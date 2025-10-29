#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/debug.h"
#include "driverlib/fpu.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/systick.h"
#include "driverlib/timer.h"
#include "driverlib/uart.h"
#include "driverlib/adc.h"  
#include "usblib/usblib.h"
#include "usblib/usb-ids.h"
#include "usblib/device/usbdevice.h"
#include "usblib/device/usbdbulk.h"
#include "utils/uartstdio.h"
#include "utils/ustdlib.h"
#include "usb_bulk_structs.h"
#include "inc/hw_adc.h"
#include "driverlib/udma.h"

#define SYSTICKS_PER_SECOND     100
#define SYSTICK_PERIOD_MS       (1000 / SYSTICKS_PER_SECOND)
#define SAMPLING_FREQ 48000
volatile uint32_t g_ui32SysTickCount = 0;
volatile uint32_t g_ui32TxCount = 0;
volatile uint32_t g_ui32RxCount = 0;
#ifdef DEBUG
uint32_t g_ui32UARTRxErrors = 0;
#endif

#ifdef DEBUG
#define DEBUG_PRINT UARTprintf
#else
#define DEBUG_PRINT while(0) ((int (*)(char *, ...))0)
#endif

#define COMMAND_PACKET_RECEIVED 0x00000001
#define COMMAND_STATUS_UPDATE   0x00000002
volatile uint32_t g_ui32Flags = 0;
static volatile bool g_bUSBConfigured = false;

#ifdef DEBUG
void __error__(char *pcFilename, uint32_t ui32Line)
{
    UARTprintf("Error at line %d of %s\n", ui32Line, pcFilename);
    while(1);
}
#endif
#define ADC_SAMPLE_BUF_SIZE 64
uint8_t pCtlTable[1024] __attribute__ ((aligned(1024)));
uint16_t ADCbuffer1[ADC_SAMPLE_BUF_SIZE];
uint16_t ADCbuffer2[ADC_SAMPLE_BUF_SIZE];
enum state{EMPTY,FILLING,FULL};
enum state bufferstatus[2];
static uint32_t g_ui32DMAErrCount = 0u;

void
uDMAErrorHandler(void)
{
    uint32_t ui32Status;
    ui32Status = uDMAErrorStatusGet();
    if(ui32Status)
    {
        uDMAErrorStatusClear();
        g_ui32DMAErrCount++;
    }
    
}

void ADC0Seq3Handler(void){
    ADCIntClear(ADC0_BASE,3);
    if ((uDMAChannelModeGet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT) ==
                            UDMA_MODE_STOP) &&
                           (bufferstatus[0] == FILLING))
    {
        bufferstatus[0] = FULL;
        bufferstatus[1] = FILLING;
    }
    else if ((uDMAChannelModeGet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT) ==
                                 UDMA_MODE_STOP) &&
                                (bufferstatus[1] == FILLING))
    {
        bufferstatus[0] = FILLING;
        bufferstatus[1] = FULL;
    }
}

void SysTickIntHandler(void)
{
    g_ui32SysTickCount++;
}

uint32_t TxHandler(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue, void *pvMsgData)
{
    if(ui32Event == USB_EVENT_TX_COMPLETE)
        g_ui32TxCount += ui32MsgValue;
    return 0;
}

uint32_t RxHandler(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue, void *pvMsgData)
{
    switch(ui32Event)
    {
        case USB_EVENT_CONNECTED:
            g_bUSBConfigured = true;
            UARTprintf("Host connected.\n");
            USBBufferFlush(&g_sTxBuffer);
            USBBufferFlush(&g_sRxBuffer);
            break;

        case USB_EVENT_DISCONNECTED:
            g_bUSBConfigured = false;
            UARTprintf("Host disconnected.\n");
            break;

        default:
            break;
    }
    return 0;
}

void ConfigureUART(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTClockSourceSet(UART0_BASE, UART_CLOCK_PIOSC);
    UARTStdioConfig(0, 115200, 16000000);
    UARTprintf("\nTiva C Series USB bulk device (ADC streaming)\r\n");
}

void ConfigureADC(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_0);

    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0));

    ADCSequenceDisable(ADC0_BASE, 3);
    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_TIMER, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_CH3 | ADC_CTL_IE | ADC_CTL_END);
    ADCSequenceEnable(ADC0_BASE, 3);
    ADCSequenceDMAEnable(ADC0_BASE, 3);
    ADCIntClear(ADC0_BASE, 3);
    ADCIntEnable(ADC0_BASE, 3);
    IntEnable(INT_ADC0SS3);
}

void ConfigureDMA(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_UDMA));
    uDMAEnable();
    uDMAControlBaseSet(pCtlTable);
    uDMAChannelAssign(UDMA_CH17_ADC0_3);
    uDMAChannelAttributeDisable(UDMA_CHANNEL_ADC3, UDMA_ATTR_ALL);
    uDMAChannelControlSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                        UDMA_SIZE_16 | UDMA_SRC_INC_NONE | UDMA_DST_INC_16 | UDMA_ARB_1);
    uDMAChannelControlSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                        UDMA_SIZE_16 | UDMA_SRC_INC_NONE | UDMA_DST_INC_16 | UDMA_ARB_1);

    uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                        UDMA_MODE_PINGPONG,
                        (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                        ADCbuffer1, ADC_SAMPLE_BUF_SIZE);

    uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                        UDMA_MODE_PINGPONG,
                        (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                        ADCbuffer2, ADC_SAMPLE_BUF_SIZE);

    uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC3, UDMA_ATTR_USEBURST);
    uDMAChannelEnable(UDMA_CHANNEL_ADC3);
    IntEnable(INT_UDMAERR);
}

int main(void)
{
    bufferstatus[0] = FILLING;
    bufferstatus[1] = EMPTY;
    FPULazyStackingEnable();
    SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
                       SYSCTL_XTAL_16MHZ);

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_3 | GPIO_PIN_2);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);

    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UDMA));
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0));
    
    ConfigureDMA();
    ConfigureUART();
    ConfigureADC(); 
    TimerConfigure(TIMER0_BASE,TIMER_CFG_SPLIT_PAIR|TIMER_CFG_A_PERIODIC);
    TimerLoadSet(TIMER0_BASE,TIMER_A,(SysCtlClockGet()/SAMPLING_FREQ)-1);
    TimerControlTrigger(TIMER0_BASE,TIMER_A,true);
    IntMasterEnable();
    TimerEnable(TIMER0_BASE,TIMER_A);

    g_bUSBConfigured = false;

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    GPIOPinTypeUSBAnalog(GPIO_PORTD_BASE, GPIO_PIN_4 | GPIO_PIN_5);

    SysTickPeriodSet(SysCtlClockGet() / SYSTICKS_PER_SECOND);
    SysTickIntEnable();
    SysTickEnable();
    UARTprintf("---------------------------------\r\n");
    UARTprintf("Configuring USB\n");

    SysCtlPeripheralEnable(SYSCTL_PERIPH_USB0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_USB0));
    USBBufferInit(&g_sTxBuffer);
    USBBufferInit(&g_sRxBuffer);
    USBStackModeSet(0, eUSBModeForceDevice, 0);
    USBDBulkInit(0, &g_sBulkDevice);

    UARTprintf("Waiting for host...\n");
    uint32_t lastTick = g_ui32SysTickCount;
    uint32_t samplesSent = 0;
    uint32_t bytesSent = 0;

    while(1)
    {   
        if (bufferstatus[0] == FULL)
        {
            bufferstatus[0] = EMPTY;
            uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT,
                                UDMA_MODE_PINGPONG,
                                (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                                ADCbuffer1, ADC_SAMPLE_BUF_SIZE);
            uDMAChannelEnable(UDMA_CHANNEL_ADC3 | UDMA_PRI_SELECT);
            uint32_t sent = USBBufferWrite(&g_sTxBuffer, (uint8_t*)ADCbuffer1, ADC_SAMPLE_BUF_SIZE * sizeof(uint16_t));
            bytesSent += sent;
            samplesSent += ADC_SAMPLE_BUF_SIZE;
        }

        if (bufferstatus[1] == FULL)
        {
            bufferstatus[1] = EMPTY;
            uDMAChannelTransferSet(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT,
                                UDMA_MODE_PINGPONG,
                                (void *)(ADC0_BASE + ADC_O_SSFIFO3),
                                ADCbuffer2, ADC_SAMPLE_BUF_SIZE);
            uDMAChannelEnable(UDMA_CHANNEL_ADC3 | UDMA_ALT_SELECT);
            uint32_t sent = USBBufferWrite(&g_sTxBuffer, (uint8_t*)ADCbuffer2, ADC_SAMPLE_BUF_SIZE * sizeof(uint16_t));
            bytesSent += sent;
            samplesSent += ADC_SAMPLE_BUF_SIZE;
        }

        if (g_ui32SysTickCount - lastTick >= 100)
        {
            UARTprintf("\r\nSamps/s: %u kS/s | Throughput: %u kB/s\r\n",
                    samplesSent / 1000, bytesSent / 1024);

            samplesSent = 0;
            bytesSent = 0;
            lastTick = g_ui32SysTickCount;
        }
    }
}
