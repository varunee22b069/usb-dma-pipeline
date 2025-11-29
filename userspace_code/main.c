#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <time.h>
#include <math.h>
#include <fftw3.h>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;
#define VENDOR_ID   0x1CBE   // TI VID
#define PRODUCT_ID  0x0003   // BULK PID
#define BULK_EP_IN  0x81     // Endpoint address (IN)
#define SAMPLES_PER_PACKET 32
#define PACKET_SIZE    64      // Endpoint max packet size (bytes)
#define SAMPLING_FREQ 48000
#define K 5
#define FFT_PACKETS 64
#define FFT_BUF_SIZE (SAMPLES_PER_PACKET*FFT_PACKETS)
#define PLOT_RATE 50
#define LOG_ENTRY_SIZE 32 
#define PAYLOAD_SIZE 64 //bytes
#define NLOG_ENTRIES FFT_BUF_SIZE/LOG_ENTRY_SIZE
#define MAX_FILE_SIZE 1ULL<<28

enum state{EMPTY,FILLING,FULL,PROCESSING};
mutex qmtx,compress_mtx;
condition_variable cv_fft,cv_log,cv_compress;
atomic<bool> running(true);
enum state state[2] = {FILLING, EMPTY};
uint16_t *voltages;
uint16_t buffer[2][FFT_BUF_SIZE];
double fft[FFT_BUF_SIZE];
uint16_t* samples;
int consumers = 0;
typedef struct {
    uint16_t marker;
    uint32_t seq_no;
    uint16_t size;
} logHeader;
FILE* logptr;
char logfile_name[50] = "";
bool file_completed = 0;

void fft_worker(){
    FILE *pipe = popen("python3 plotter.py","w");
    if(!pipe){
        perror("popen failed!");
        return;
    }
    fftw_complex* out = (fftw_complex*) fftw_malloc((FFT_BUF_SIZE/2+1) * sizeof(fftw_complex));
    fftw_plan p = fftw_plan_dft_r2c_1d(FFT_BUF_SIZE,nullptr,out,FFTW_ESTIMATE);
    while(running){
        int N = FFT_BUF_SIZE;
        priority_queue<pair<double,double>, vector<pair<double,double>>, greater<pair<double,double>>> topK;
        {
            unique_lock<mutex> lock(qmtx);
            cv_fft.wait(lock,[]{return !running || (state[0] == FULL || state[1] == FULL);});
            if(state[0] == FULL){
                samples = buffer[0];
                state[0] = PROCESSING;
            }
            else{
                samples = buffer[1];
                state[1] = PROCESSING;
            }
            consumers++;
            cv_log.notify_one();
        }
        for(int i = 0;i < FFT_BUF_SIZE;i++)
            fft[i] = samples[i]*3.3/4096;

        fftw_execute_dft_r2c(p,fft,out);
        {
            unique_lock<mutex> lock(qmtx);
            consumers--;
            if(!consumers){
                if(state[0] == PROCESSING) state[0] = EMPTY;
                else state[1] = EMPTY; 
            }
        }
        for (int i = 0; i < N/2 + 1; i++) {
            double mag = 2*sqrt(out[i][0]*out[i][0] + out[i][1]*out[i][1])/N;
            double freq = i * SAMPLING_FREQ / (double)N;
            if (i == 0 || (N % 2 == 0 && i == N/2))
                mag /= 2.0;
            topK.push({mag,freq});
            if(topK.size() > K) topK.pop();
        }
        while(!topK.empty()){
            pair<double,double> temp = topK.top();
            fprintf(pipe, "%.3lf %.3lf ", temp.second, temp.first);
            topK.pop();
        }
        fprintf(pipe,"\n");
        fflush(pipe);
    }
    pclose(pipe);
    fftw_destroy_plan(p);
    fftw_cleanup();
}
void compressor(){
    while(running){
        {
            unique_lock<mutex>lock(compress_mtx);
            cv_compress.wait(lock,[]{return file_completed;});
            file_completed = 0;
        }
        if(!running) return;
        char temp[100];
        sprintf(temp,"gzip -9 %s",logfile_name);
        system(temp);
    }
}

void logger(){
    uint32_t seq_num = 0;
    int fileNum = 0;
    unsigned long long bytes_written = 0;
    uint16_t *logBuffer;
    char name[50] = "log_0.bin";
    logptr = fopen(name,"a+");
    while(running){
        {
            unique_lock<mutex>lock(qmtx);
            cv_log.wait(lock,[]{return !running || (state[0] == PROCESSING || state[1] == PROCESSING);});
            if(!running) return;
            consumers++;
        }
        for(int i = 0; i < NLOG_ENTRIES;i++){
            logHeader header = {0xBEEF,seq_num,LOG_ENTRY_SIZE};
            fwrite(&header,1,sizeof(header),logptr);
            fwrite(samples + i*LOG_ENTRY_SIZE,1,PAYLOAD_SIZE,logptr);
            bytes_written+= (sizeof(header)+PAYLOAD_SIZE);
            seq_num++;
        }
        fflush(logptr);
        if(bytes_written > MAX_FILE_SIZE){
            strcpy(logfile_name,name);
            fclose(logptr);
            fileNum++;
            sprintf(name,"log_%d.bin",fileNum);
            logptr = fopen(name,"a+");
            {
                unique_lock<mutex>lock(compress_mtx);
                file_completed = 1;
            } 
            cv_compress.notify_one();
        }
        {
            unique_lock<mutex> lock(qmtx);
            consumers--;
            if(!consumers){
                if(state[0] == PROCESSING) state[0] = EMPTY;
                else state[1] = EMPTY; 
            }
        }
    }
    fclose(logptr);
}

int main(void){
    libusb_context *ctx = NULL;
    libusb_device **list;
    if (libusb_init(&ctx) < 0) { fprintf(stderr, "libusb init failed\n"); return 1; } 
    ssize_t cnt = libusb_get_device_list(ctx,&list);
    libusb_device_handle *dev = libusb_open_device_with_vid_pid(ctx,VENDOR_ID,PRODUCT_ID);
    if(!dev){
        fprintf(stderr,"Device not found");
        libusb_exit(ctx); 
        return 2;
    }
    printf("Device opened successfully!\n");
    libusb_set_auto_detach_kernel_driver(dev,1);
    int claim = libusb_claim_interface(dev,0);
    if(claim < 0){
        fprintf(stderr, "Cannot claim interface: %s\n",libusb_error_name(claim));
        libusb_close(dev);
        libusb_exit(ctx);
        return 3;
    }
    printf("usb interface claimed\n");
    
    unsigned char data[PACKET_SIZE] = {0};
    int length = 0;
    int err = 0;
    struct timespec start, now;
    unsigned long long int packets = 0;
    unsigned long long int bytes = 0;
    unsigned long long int sum = 0;
    unsigned long long int drops = 0;
    double totalPackets = 0;
    unsigned long long int totalDrops = 0;
    voltages = buffer[0];   
    int ind = 0;
    thread fft_thread(fft_worker);
    thread log_thread(logger);
    thread compressor_thread(compressor);
    clock_gettime(CLOCK_MONOTONIC,&start);
    while(1){
        clock_gettime(CLOCK_MONOTONIC,&now);
        double elapsed = now.tv_sec-start.tv_sec + (now.tv_nsec-start.tv_nsec)/1e9;
        if(elapsed>0.5){
            printf("packets: %.1f/s \t drops: %lf%% \t bytes: %.1fkB/s \t voltage: %.3lf\n",
                    packets/elapsed,(totalDrops/totalPackets)*100, bytes/(1000*elapsed), (3.3 * sum / 4096.0) / (bytes / 2.0));
            packets = 0;
            bytes = 0;
            sum = 0;
            clock_gettime(CLOCK_MONOTONIC,&start);
        }
        err = libusb_bulk_transfer(dev,BULK_EP_IN,data,PACKET_SIZE,&length,1000);
        if(!err){
            packets++;
            totalPackets++;
            bytes += length;
            
            if(length!=64) totalDrops++;
            for(int i = 0; i < length/2; i++){
                voltages[ind] = data[2*i] | (data[2*i+1]<< 8);
                voltages[ind]&= 0x0FFF;
                sum+=voltages[ind];
                ind++;
                if(ind == FFT_BUF_SIZE){
                    {
                    unique_lock<mutex> lock(qmtx);
                    if(voltages == buffer[0]){
                        state[0] = FULL;
                        state[1] = FILLING;
                        voltages = buffer[1];
                    }
                    else{
                        state[1] = FULL;
                        state[0] = FILLING;
                        voltages = buffer[0];
                    }
                }
                ind = 0;
                cv_fft.notify_one();
                }
            }
        }
        else if (err == LIBUSB_ERROR_TIMEOUT) {
            printf("Timeout — no data received within 1s.\n");
        } else {
            fprintf(stderr, "Transfer error: %s\n", libusb_error_name(err));
        }
    }
    running = false;
    cv_fft.notify_one();
    cv_log.notify_one();
    cv_compress.notify_one();
    fft_thread.join();
    log_thread.join();
    compressor_thread.join();
    libusb_release_interface(dev,0);
    libusb_close(dev);
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
}
