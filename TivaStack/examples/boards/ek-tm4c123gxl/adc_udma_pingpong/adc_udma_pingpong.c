#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_adc.h"
#include "inc/hw_types.h"
#include "inc/hw_udma.h"
#include "driverlib/adc.h"
#include "driverlib/debug.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/systick.h"
#include "driverlib/timer.h"
#include "driverlib/uart.h"
#include "driverlib/udma.h"
#include "utils/uartstdio.h"
#define ADC_SAMPLE_BUF_SIZE     64
#if defined(ewarm)
#pragma data_alignment=1024
uint8_t pui8ControlTable[1024];
#elif defined(ccs)
#pragma DATA_ALIGN(pui8ControlTable, 1024)
uint8_t pui8ControlTable[1024];
#else
uint8_t pui8ControlTable[1024] __attribute__ ((aligned(1024)));
#endif
static uint16_t pui16ADCBuffer1[ADC_SAMPLE_BUF_SIZE];
static uint16_t pui16ADCBuffer2[ADC_SAMPLE_BUF_SIZE];
enum BUFFER_STATUS
{
    EMPTY,
    FILLING,
    FULL
};
static enum BUFFER_STATUS pui32BufferStatus[2];
static uint32_t g_ui32DMAErrCount = 0u;
#ifdef DEBUG
void
__error__(char *pcFilename, uint32_t ui32Line)
{
}
#endif
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
void
ADCSeq0Handler(void)
{
    ADCIntClear(ADC0_BASE, 0);
    if ((uDMAChannelModeGet(UDMA_CHANNEL_ADC0 | UDMA_PRI_SELECT) ==
                            UDMA_MODE_STOP) &&
                           (pui32BufferStatus[0] == FILLING))
    {
        pui32BufferStatus[0] = FULL;
        pui32BufferStatus[1] = FILLING;
    }
    else if ((uDMAChannelModeGet(UDMA_CHANNEL_ADC0 | UDMA_ALT_SELECT) ==
                                 UDMA_MODE_STOP) &&
                                (pui32BufferStatus[1] == FILLING))
    {
        pui32BufferStatus[0] = FILLING;
        pui32BufferStatus[1] = FULL;
    }
}
void
ConfigureUART(void)
{
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    MAP_GPIOPinConfigure(GPIO_PA0_U0RX);
    MAP_GPIOPinConfigure(GPIO_PA1_U0TX);
    MAP_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    MAP_UARTClockSourceSet(UART0_BASE, UART_CLOCK_PIOSC);
    UARTStdioConfig(0, 115200, 16000000);
}
int
main(void)
{
    uint32_t ui32Count, ui32AverageResult1, ui32AverageResult2;
    uint32_t ui32SamplesTaken = 0;
    MAP_SysCtlClockSet(SYSCTL_SYSDIV_2_5 | SYSCTL_USE_PLL | SYSCTL_OSC_INT |
                       SYSCTL_XTAL_16MHZ);
    pui32BufferStatus[0] = FILLING;
    pui32BufferStatus[1] = EMPTY;
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    MAP_GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_0);
    ConfigureUART();
    UARTprintf("Timer->ADC->uDMA demo!\n\n");
    UARTprintf("ui32AverageResult1\tui32AverageResult2\tTotal Samples\n");
    uDMAEnable();
    uDMAControlBaseSet(pui8ControlTable);
    uDMAChannelAttributeDisable(UDMA_CHANNEL_ADC0,
                                UDMA_ATTR_ALTSELECT | UDMA_ATTR_HIGH_PRIORITY |
                                UDMA_ATTR_REQMASK);
    uDMAChannelControlSet(UDMA_CHANNEL_ADC0 | UDMA_PRI_SELECT, UDMA_SIZE_16 |
                          UDMA_SRC_INC_NONE | UDMA_DST_INC_16 | UDMA_ARB_1);
    uDMAChannelControlSet(UDMA_CHANNEL_ADC0 | UDMA_ALT_SELECT, UDMA_SIZE_16 |
                          UDMA_SRC_INC_NONE | UDMA_DST_INC_16 | UDMA_ARB_1);
    uDMAChannelTransferSet(UDMA_CHANNEL_ADC0 | UDMA_PRI_SELECT,
                           UDMA_MODE_PINGPONG,
                           (void *)(ADC0_BASE + ADC_O_SSFIFO0),
                           &pui16ADCBuffer1, ADC_SAMPLE_BUF_SIZE);
    uDMAChannelTransferSet(UDMA_CHANNEL_ADC0 | UDMA_ALT_SELECT,
                           UDMA_MODE_PINGPONG,
                           (void *)(ADC0_BASE + ADC_O_SSFIFO0),
                           &pui16ADCBuffer2, ADC_SAMPLE_BUF_SIZE);
    uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC0, UDMA_ATTR_USEBURST);
    uDMAChannelEnable(UDMA_CHANNEL_ADC0);
    ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PIOSC | ADC_CLOCK_RATE_HALF, 1);
    SysCtlDelay(10);
    IntDisable(INT_ADC0SS0);
    ADCIntDisable(ADC0_BASE, 0);
    ADCSequenceDisable(ADC0_BASE, 0);
    ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_TIMER, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH3 | ADC_CTL_END |
                             ADC_CTL_IE);
    ADCSequenceEnable(ADC0_BASE, 0);
    ADCIntClear(ADC0_BASE, 0);
    ADCSequenceDMAEnable(ADC0_BASE, 0);
    ADCIntEnable(ADC0_BASE, 0);
    IntEnable(INT_ADC0SS0);
    TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PERIODIC);
    TimerLoadSet(TIMER0_BASE, TIMER_A, (SysCtlClockGet()/16000) - 1);
    TimerControlTrigger(TIMER0_BASE, TIMER_A, true);
    IntMasterEnable();
    TimerEnable(TIMER0_BASE, TIMER_A);
    while(1)
    {
        if(pui32BufferStatus[0] == FULL)
        {
            ui32AverageResult1 = 0;
            for(ui32Count = 0; ui32Count < ADC_SAMPLE_BUF_SIZE; ui32Count++)
            {
                ui32AverageResult1 += pui16ADCBuffer1[ui32Count];
                pui16ADCBuffer1[ui32Count] = 0;
            }
            pui32BufferStatus[0] = EMPTY;
            uDMAChannelTransferSet(UDMA_CHANNEL_ADC0 | UDMA_PRI_SELECT,
                                   UDMA_MODE_PINGPONG,
                                   (void *)(ADC0_BASE + ADC_O_SSFIFO0),
                                   &pui16ADCBuffer1, ADC_SAMPLE_BUF_SIZE);
            uDMAChannelEnable(UDMA_CHANNEL_ADC0 | UDMA_PRI_SELECT);
            ui32SamplesTaken += ADC_SAMPLE_BUF_SIZE;
            ui32AverageResult1 = ((ui32AverageResult1 +
                                  (ADC_SAMPLE_BUF_SIZE / 2)) /
                                   ADC_SAMPLE_BUF_SIZE);
        }
        if(pui32BufferStatus[1] == FULL)
        {
            ui32AverageResult2 = 0;
            for(ui32Count =0; ui32Count < ADC_SAMPLE_BUF_SIZE; ui32Count++)
            {
                ui32AverageResult2 += pui16ADCBuffer2[ui32Count];
                pui16ADCBuffer2[ui32Count] = 0;
            }
            pui32BufferStatus[1] = EMPTY;
            uDMAChannelTransferSet(UDMA_CHANNEL_ADC0 | UDMA_ALT_SELECT,
                                   UDMA_MODE_PINGPONG,
                                   (void *)(ADC0_BASE + ADC_O_SSFIFO0),
                                   &pui16ADCBuffer2, ADC_SAMPLE_BUF_SIZE);
            uDMAChannelEnable(UDMA_CHANNEL_ADC0 | UDMA_ALT_SELECT);
            ui32SamplesTaken += ADC_SAMPLE_BUF_SIZE;
            ui32AverageResult2 = ((ui32AverageResult2 +
                                  (ADC_SAMPLE_BUF_SIZE / 2)) /
                                   ADC_SAMPLE_BUF_SIZE);
            UARTprintf("\t%4d\t\t\t%4d\t\t%d\r", ui32AverageResult1,
                       ui32AverageResult2, ui32SamplesTaken);
        }
    }
}