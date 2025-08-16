
PART=TM4C123GH6PM

#
# The base directory for TivaWare.
#
ROOT=../../../..

#
# Include the common make definitions.
#
include ${ROOT}/makedefs

#
# Where to find source files that do not live in this directory.
#
VPATH=../../../../utils

#
# Where to find header files that do not live in the source directory.
#
IPATH=../../../..

#
# The default rule, which causes the ADC w/ Timer Trigger and uDMA Ping-Pong Mode example to be built.
#
all: ${COMPILER}
all: ${COMPILER}/adc_udma_pingpong.axf

#
# The rule to clean out all the build products.
#
clean:
	@rm -rf ${COMPILER} ${wildcard *~}

#
# The rule to create the target directory.
#
${COMPILER}:
	@mkdir -p ${COMPILER}

#
# Rules for building the ADC w/ Timer Trigger and uDMA Ping-Pong Mode example.
#
${COMPILER}/adc_udma_pingpong.axf: ${COMPILER}/adc_udma_pingpong.o
${COMPILER}/adc_udma_pingpong.axf: ${COMPILER}/startup_${COMPILER}.o
${COMPILER}/adc_udma_pingpong.axf: ${COMPILER}/uartstdio.o
${COMPILER}/adc_udma_pingpong.axf: ${ROOT}/driverlib/${COMPILER}/libdriver.a
${COMPILER}/adc_udma_pingpong.axf: adc_udma_pingpong.ld
SCATTERgcc_adc_udma_pingpong=adc_udma_pingpong.ld
ENTRY_adc_udma_pingpong=ResetISR
CFLAGSgcc=-DTARGET_IS_TM4C123_RB1

#
# Include the automatically generated dependency files.
#
ifneq (${MAKECMDGOALS},clean)
-include ${wildcard ${COMPILER}/*.d} __dummy__
endif
