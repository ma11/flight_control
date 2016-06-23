#include "stm32f303xc.h"
#include "fc_reg.h"
#include "mpu_reg.h"

#define MOTOR_PULSE_MAX 2100
#define FLAG_SENSOR 0x01
#define FLAG_COMMAND 0x02
#define FLAG_I2C_RD_WRN 0x04
#define FLAG_I2C_RETURN 0x08
#define FLAG_UPDATE_REG 0x10

#define FPU_CPACR ((uint32_t *) 0xE000ED88)

volatile uint8_t FLAG;

volatile uint32_t tick;

volatile uint8_t uart3_rx_buffer[16];
volatile uint8_t uart3_tx_buffer[16];
volatile uint8_t uart2_rx_buffer[16];
volatile uint8_t i2c2_rx_buffer[16];
volatile uint8_t i2c2_tx_buffer[16];

volatile uint16_t sensor_sample_count;
volatile int16_t gyro_x_raw;
volatile int16_t gyro_y_raw;
volatile int16_t gyro_z_raw;
volatile int16_t accel_x_raw;
volatile int16_t accel_y_raw;
volatile int16_t accel_z_raw;
//volatile int16_t temp_raw;

volatile uint16_t command_frame_count;
//volatile uint8_t command_fades;
//volatile uint8_t command_system;
volatile uint16_t throttle_raw;
volatile uint16_t aileron_raw;
volatile uint16_t elevator_raw;
volatile uint16_t rudder_raw;
volatile uint16_t armed_raw;

void SetDmaUart3Tx(uint8_t size)
{
	DMA1_Channel2->CCR = DMA_CCR_MINC | DMA_CCR_DIR;
	DMA1_Channel2->CMAR = (uint32_t)uart3_tx_buffer;
	DMA1_Channel2->CPAR = (uint32_t)&(USART3->TDR);
	DMA1_Channel2->CNDTR = size;
	DMA1_Channel2->CCR |= DMA_CCR_EN;
}

void SetDmaUart3Rx()
{
	DMA1_Channel3->CCR = DMA_CCR_TCIE | DMA_CCR_MINC;
	DMA1_Channel3->CMAR = (uint32_t)uart3_rx_buffer;
	DMA1_Channel3->CPAR = (uint32_t)&(USART3->RDR);
	DMA1_Channel3->CNDTR = 6;
	DMA1_Channel3->CCR |= DMA_CCR_EN;
}

void SetDmaI2c2Tx(uint8_t size)
{
	I2C2->CR1 &= ~I2C_CR1_PE;
	I2C2->CR2 &= ~(I2C_CR2_RD_WRN | I2C_CR2_NBYTES_Msk);
	
	DMA1_Channel4->CCR = DMA_CCR_MINC | DMA_CCR_DIR;
	DMA1_Channel4->CMAR = (uint32_t)i2c2_tx_buffer;
	DMA1_Channel4->CPAR = (uint32_t)&(I2C2->TXDR);
	DMA1_Channel4->CNDTR = size;
	DMA1_Channel4->CCR |= DMA_CCR_EN;
	
	I2C2->CR1 |= I2C_CR1_PE;
	I2C2->CR2 |= (I2C_CR2_NBYTES_Msk & (size << I2C_CR2_NBYTES_Pos)) | I2C_CR2_START;
}

void SetDmaI2c2Rx(uint8_t size)
{
	DMA1_Channel5->CCR = DMA_CCR_MINC;
	DMA1_Channel5->CMAR = (uint32_t)i2c2_rx_buffer;
	DMA1_Channel5->CPAR = (uint32_t)&(I2C2->RXDR);
	DMA1_Channel5->CNDTR = size;
	DMA1_Channel5->CCR |= DMA_CCR_EN;
}

void SetDmaUart2Rx()
{
	DMA1_Channel6->CCR = DMA_CCR_TCIE | DMA_CCR_MINC;
	DMA1_Channel6->CMAR = (uint32_t)uart2_rx_buffer;
	DMA1_Channel6->CPAR = (uint32_t)&(USART2->RDR);
	DMA1_Channel6->CNDTR = 16;
	DMA1_Channel6->CCR |= DMA_CCR_EN;
}

void Wait(uint32_t ticks)
{
	uint32_t current_tick;
	uint32_t next_tick;
	current_tick = tick;
	next_tick = current_tick + ticks;
	while (tick != next_tick)
		__WFI();
}

void I2cWrite(uint8_t addr, uint8_t data)
{
	i2c2_tx_buffer[0] = addr;
	i2c2_tx_buffer[1] = data;
	SetDmaI2c2Tx(2);
	Wait(1);
}

void I2cRead(uint8_t addr, uint8_t * data)
{
	FLAG |= FLAG_I2C_RD_WRN;
	SetDmaI2c2Rx(1);
	i2c2_tx_buffer[0] = addr;
	SetDmaI2c2Tx(1);
	Wait(1);
	*data = i2c2_rx_buffer[0];
}

void bytes_to_float(volatile uint32_t * b, float * f)
{
	union {
		float f;
		uint32_t b;
	} f2b;
	f2b.b = *b;
	*f = f2b.f;
}

void float_to_bytes(float * f, volatile uint8_t * b)
{
	union {
		float f;
		uint8_t b[4];
	} f2b;
	f2b.f = *f;
	b[0] = f2b.b[0];
	b[1] = f2b.b[1];
	b[2] = f2b.b[2];
	b[3] = f2b.b[3];
}

//######## System Interrupts ########

void SysTick_Handler()
{
	tick++;
}

//####### External Interrupts ########

void EXTI15_10_IRQHandler()
{
	EXTI->PR = EXTI_PR_PIF15; // Clear pending request
	
	if (REG_CTRL__MPU == 0)
	{
		sensor_sample_count++;
		
		if (REG_CTRL__LED == 2)
		{
			if ((sensor_sample_count & 0x7F) == 0)
				GPIOB->BSRR = GPIO_BSRR_BR_5;
			else if ((sensor_sample_count & 0x7F) == 64)
				GPIOB->BSRR = GPIO_BSRR_BS_5;
		}
		
		accel_x_raw = ((int16_t)i2c2_rx_buffer[0]  << 8) | (int16_t)i2c2_rx_buffer[1];
		accel_y_raw = ((int16_t)i2c2_rx_buffer[2]  << 8) | (int16_t)i2c2_rx_buffer[3];
		accel_z_raw = ((int16_t)i2c2_rx_buffer[4]  << 8) | (int16_t)i2c2_rx_buffer[5];
	  //temp_raw    = ((int16_t)i2c2_rx_buffer[6]  << 8) | (int16_t)i2c2_rx_buffer[7];
		gyro_x_raw  = ((int16_t)i2c2_rx_buffer[8]  << 8) | (int16_t)i2c2_rx_buffer[9];
		gyro_y_raw  = ((int16_t)i2c2_rx_buffer[10] << 8) | (int16_t)i2c2_rx_buffer[11];
		gyro_z_raw  = ((int16_t)i2c2_rx_buffer[12] << 8) | (int16_t)i2c2_rx_buffer[13];
		
		FLAG |= (FLAG_I2C_RD_WRN | FLAG_SENSOR);
		SetDmaI2c2Rx(14);
		i2c2_tx_buffer[0] = 59;
		SetDmaI2c2Tx(1);
	}
}

void I2C2_EV_IRQHandler()
{
	if (FLAG & FLAG_I2C_RD_WRN)
	{
		FLAG &= ~FLAG_I2C_RD_WRN;
		I2C2->CR2 &= ~I2C_CR2_NBYTES_Msk;
		I2C2->CR2 |= (I2C_CR2_NBYTES_Msk & (DMA1_Channel5->CNDTR << I2C_CR2_NBYTES_Pos)) | I2C_CR2_RD_WRN | I2C_CR2_START;
	}
	else
	{
		I2C2->CR2 |= I2C_CR2_STOP;
		if (FLAG & FLAG_I2C_RETURN)
		{
			FLAG &= ~FLAG_I2C_RETURN;
			uart3_tx_buffer[0] = i2c2_rx_buffer[0];
			SetDmaUart3Tx(1);
		}
	}
}

void DMA1_Channel3_IRQHandler()
{
	uint8_t cmd;
	uint8_t addr;
	
	DMA1->IFCR = DMA_IFCR_CTCIF3; // clear flag
	
	cmd = uart3_rx_buffer[0];
	addr = uart3_rx_buffer[1];
	
	if (cmd == 0)
	{
		uart3_tx_buffer[0] = (uint8_t)((reg[addr] >> 24) & 0xFF);
		uart3_tx_buffer[1] = (uint8_t)((reg[addr] >> 16) & 0xFF);
		uart3_tx_buffer[2] = (uint8_t)((reg[addr] >>  8) & 0xFF);
		uart3_tx_buffer[3] = (uint8_t)((reg[addr] >>  0) & 0xFF);
		SetDmaUart3Tx(4);
	}
	else if (cmd == 1)
	{
		reg[addr] = ((uint32_t)uart3_rx_buffer[2] << 24) | ((uint32_t)uart3_rx_buffer[3] << 16) | ((uint32_t)uart3_rx_buffer[4] << 8) | (uint32_t)uart3_rx_buffer[5];
		FLAG |= FLAG_UPDATE_REG;
	}
	else if (cmd == 2)
	{
		FLAG |= FLAG_I2C_RD_WRN | FLAG_I2C_RETURN;
		SetDmaI2c2Rx(1);
		i2c2_tx_buffer[0] = addr;
		SetDmaI2c2Tx(1);
	}
	else if (cmd == 3)
	{
		i2c2_tx_buffer[0] = addr;
		i2c2_tx_buffer[1] = uart3_rx_buffer[5];
		SetDmaI2c2Tx(2);
	}
	
	SetDmaUart3Rx();
}

void DMA1_Channel6_IRQHandler()
{
	int i;
	uint8_t chan_id;
	uint16_t servo;
	
	DMA1->IFCR = DMA_IFCR_CTCIF6; // clear flag
	
	command_frame_count++;
	
	if (REG_CTRL__LED == 3)
	{
		if ((command_frame_count & 0x3F) == 0)
			GPIOB->BSRR = GPIO_BSRR_BR_5;
		else if ((command_frame_count & 0x3F) == 32)
			GPIOB->BSRR = GPIO_BSRR_BS_5;
	}
	
	//command_fades = uart2_rx_buffer[0];
	//command_system = uart2_rx_buffer[1];
	for (i=0; i<7; i++)
	{
		servo = ((uint16_t)uart2_rx_buffer[2+2*i] << 8) | (uint16_t)uart2_rx_buffer[3+2*i];
		chan_id = (uint8_t)((servo & 0x7800) >> 11);
		switch(chan_id)
		{
			case 0:
				throttle_raw = servo & 0x07FF;
				break;
			case 1:
				aileron_raw = servo & 0x07FF;
				break;
			case 2:
				elevator_raw = servo & 0x07FF;
				break;
			case 3:
				rudder_raw = servo & 0x07FF;
				break;
			case 4:
				armed_raw = servo & 0x07FF;
				break;
		}
	}
	
	SetDmaUart2Rx();
	
	FLAG |= FLAG_COMMAND;
}

//######## MAIN #########

int main()
{
	int i;
	
	float gyro_x;
	float gyro_y;
	float gyro_z;
	
	float gyro_x_dc;
	float gyro_y_dc;
	float gyro_z_dc;
	
	float gyro_x_scale;
	float gyro_y_scale;
	float gyro_z_scale;
	
	float throttle;
	float aileron;
	float elevator;
	float rudder;
	
	float throttle_scale;
	float aileron_scale;
	float elevator_scale;
	float rudder_scale;
	
	float error_pitch;
	float error_roll;
	float error_yaw;
	float error_pitch_z;
	float error_roll_z;
	float error_yaw_z;
	float error_pitch_int;
	float error_roll_int;
	float error_yaw_int;
	
	float pitch;
	float roll;
	float yaw;
	
	float pitch_p;
	float pitch_i;
	float pitch_d;
	float roll_p;
	float roll_i;
	float roll_d;
	float yaw_p;
	float yaw_i;
	float yaw_d;
	
	float motor_1;
	float motor_2;
	float motor_3;
	float motor_4;
	
	int32_t motor_1_clip;
	int32_t motor_2_clip;
	int32_t motor_3_clip;
	int32_t motor_4_clip;
	
	uint16_t motor_1_raw;
	uint16_t motor_2_raw;
	uint16_t motor_3_raw;
	uint16_t motor_4_raw;
	
	//######## CLOCK ##########
	
	RCC->APB1ENR = RCC_APB1ENR_USART2EN | RCC_APB1ENR_USART3EN | RCC_APB1ENR_I2C2EN | RCC_APB1ENR_TIM3EN;
	RCC->APB2ENR = RCC_APB2ENR_SYSCFGEN;
	RCC->AHBENR |= RCC_AHBENR_DMA1EN | RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN;
	SystemCoreClockUpdate();
	SysTick_Config(SystemCoreClock/1000); // 1 ms
	
	//####### VAR AND REG INIT #######
	
	FLAG = FLAG_UPDATE_REG;
	for(i=0; i<REG_NB_ADDR; i++)
		reg[i] = reg_init[i];
	command_frame_count = 0;
	throttle = 0;
	aileron = 0;
	elevator = 0;
	rudder = 0;
	sensor_sample_count = 0;
	error_pitch_int = 0;
	error_roll_int = 0;
	error_yaw_int = 0;
	pitch = 0;
	roll = 0;
	yaw = 0;
	
	//####### BIND ########
	
	Wait(100);
	
	GPIOB->BSRR = GPIO_BSRR_BS_4;
	GPIOB->MODER &= ~GPIO_MODER_MODER4_Msk;
	GPIOB->MODER |= GPIO_MODER_MODER4_0; // Output GP
	GPIOB->OTYPER &= ~GPIO_OTYPER_OT_4; // PP
	GPIOB->OSPEEDR &= ~GPIO_OSPEEDER_OSPEEDR4_Msk; // Low speed
	GPIOB->PUPDR &= ~GPIO_PUPDR_PUPDR4_Msk; // No PU/PD
	for (i=0; i<9; i++)
	{
		GPIOB->BSRR = GPIO_BSRR_BR_4;
		Wait(1);
		GPIOB->BSRR = GPIO_BSRR_BS_4;
		Wait(1);
	}
	
	//####### GPIO ##########
	
	// A0 : Beep
	// A4 : Motor 2
	// A6 : Motor 1
	// A9 : I2C2 SCL
	// A10: I2C2 SDA
	// A15: MPU interrupt
	GPIOA->MODER &= ~(GPIO_MODER_MODER0_Msk | GPIO_MODER_MODER4_Msk | GPIO_MODER_MODER6_Msk | GPIO_MODER_MODER9_Msk | GPIO_MODER_MODER10_Msk | GPIO_MODER_MODER15_Msk); //Reset MODER
	GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR0_Msk | GPIO_PUPDR_PUPDR4_Msk | GPIO_PUPDR_PUPDR6_Msk | GPIO_PUPDR_PUPDR9_Msk | GPIO_PUPDR_PUPDR10_Msk | GPIO_PUPDR_PUPDR15_Msk); // Reset PUPDR
	GPIOA->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEEDR4_Msk | GPIO_OSPEEDER_OSPEEDR6_Msk | GPIO_OSPEEDER_OSPEEDR9_Msk | GPIO_OSPEEDER_OSPEEDR10_Msk); // Reset OSPEEDR for output only
	GPIOA->MODER |= GPIO_MODER_MODER4_1 | GPIO_MODER_MODER6_1 | GPIO_MODER_MODER9_1 | GPIO_MODER_MODER10_1;
	GPIOA->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR4 | GPIO_OSPEEDER_OSPEEDR6 | GPIO_OSPEEDER_OSPEEDR9 | GPIO_OSPEEDER_OSPEEDR10; // Full-speed
	GPIOA->OTYPER = GPIO_OTYPER_OT_9 | GPIO_OTYPER_OT_10; // OD
	GPIOA->AFR[0] = (GPIO_AFRL_AFRL4_Msk & (2 << GPIO_AFRL_AFRL4_Pos)) | (GPIO_AFRL_AFRL6_Msk & (2 << GPIO_AFRL_AFRL6_Pos)); // AF2 = TIM3
	GPIOA->AFR[1] = (GPIO_AFRH_AFRH1_Msk & (4 << GPIO_AFRH_AFRH1_Pos)) | (GPIO_AFRH_AFRH2_Msk & (4 << GPIO_AFRH_AFRH2_Pos)); // AF4 = I2C
	
	// B0 : Motor 3
	// B1 : Motor 4
	// B3 : UART2 Tx NOT USED
	// B4 : UART2 Rx
	// B5 : Red LED
	// B6 : UART1 Tx NOT USED
	// B7 : UART1 Rx NOT USED
	// B10: UART3 Tx
	// B11: UART3 Rx
	GPIOB->MODER &= ~(GPIO_MODER_MODER0_Msk | GPIO_MODER_MODER1_Msk | GPIO_MODER_MODER4_Msk | GPIO_MODER_MODER5_Msk | GPIO_MODER_MODER10_Msk | GPIO_MODER_MODER11_Msk); // Reset MODER
	GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPDR0_Msk | GPIO_PUPDR_PUPDR1_Msk | GPIO_PUPDR_PUPDR4_Msk | GPIO_PUPDR_PUPDR5_Msk | GPIO_PUPDR_PUPDR10_Msk | GPIO_PUPDR_PUPDR11_Msk); // Reset PUPDR
	GPIOB->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEEDR0_Msk | GPIO_OSPEEDER_OSPEEDR1_Msk | GPIO_OSPEEDER_OSPEEDR5_Msk | GPIO_OSPEEDER_OSPEEDR10_Msk); // Reset OSPEEDR for output only
	GPIOB->MODER |= GPIO_MODER_MODER0_1 | GPIO_MODER_MODER1_1 | GPIO_MODER_MODER4_1 | GPIO_MODER_MODER5_0 | GPIO_MODER_MODER10_1 | GPIO_MODER_MODER11_1;
	GPIOB->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR0 | GPIO_OSPEEDER_OSPEEDR1 | GPIO_OSPEEDER_OSPEEDR10; // Full-speed
	GPIOB->OTYPER = GPIO_OTYPER_OT_5; // OD
	GPIOB->AFR[0] = (GPIO_AFRL_AFRL0_Msk & (2 << GPIO_AFRL_AFRL0_Pos)) | (GPIO_AFRL_AFRL1_Msk & (2 << GPIO_AFRL_AFRL1_Pos)) | (GPIO_AFRL_AFRL4_Msk & (7 << GPIO_AFRL_AFRL4_Pos)); // AF2 = TIM3, AF7 = USART
	GPIOB->AFR[1] = (GPIO_AFRH_AFRH2_Msk & (7 << GPIO_AFRH_AFRH2_Pos)) | (GPIO_AFRH_AFRH3_Msk & (7 << GPIO_AFRH_AFRH3_Pos)); // AF7 = USART
	
	//######## UART ##########
	
	USART3->BRR = 69; // 115200 bps
	USART3->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;
	USART3->CR3 = USART_CR3_DMAR | USART_CR3_DMAT;
	SetDmaUart3Rx();
	
	USART2->BRR = 69; // 115200 bps
	USART2->CR1 = USART_CR1_UE | USART_CR1_RE;
	USART2->CR3 = USART_CR3_DMAR;
	SetDmaUart2Rx();
	
	//####### I2C #########
	
	I2C2->CR1 = I2C_CR1_PE | I2C_CR1_TCIE | I2C_CR1_TXDMAEN | I2C_CR1_RXDMAEN;
	I2C2->CR2 = I2C_CR2_SADD_Msk & (104 << (1+I2C_CR2_SADD_Pos));
	I2C2->TIMINGR = (I2C_TIMINGR_SCLL_Msk & (9 << I2C_TIMINGR_SCLL_Pos)) | (I2C_TIMINGR_SCLH_Msk & (3 << I2C_TIMINGR_SCLH_Pos)) | (I2C_TIMINGR_SDADEL_Msk & (1 << I2C_TIMINGR_SDADEL_Pos)) | (I2C_TIMINGR_SCLDEL_Msk & (3 << I2C_TIMINGR_SCLDEL_Pos));
	
	//####### TIM ########
	
	TIM3->CR1 = TIM_CR1_OPM;
	TIM3->PSC = 7; // 8MHz/8 = 1MHz = 1 us
	TIM3->ARR = MOTOR_PULSE_MAX;
	TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;
	TIM3->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_0;
	TIM3->CCMR2 = TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_0 | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_0;
		
	//####### INTERRUPT #######
	
	SYSCFG->EXTICR[3] = SYSCFG_EXTICR4_EXTI15_PA;
	EXTI->RTSR |= EXTI_RTSR_TR15;
	EXTI->IMR = EXTI_IMR_MR15;
	
	NVIC_EnableIRQ(EXTI15_10_IRQn);
	NVIC_EnableIRQ(I2C2_EV_IRQn);
	NVIC_EnableIRQ(DMA1_Channel3_IRQn);
	NVIC_EnableIRQ(DMA1_Channel6_IRQn);
	
	NVIC_SetPriority(EXTI15_10_IRQn,2);
	NVIC_SetPriority(I2C2_EV_IRQn,1);
	NVIC_SetPriority(DMA1_Channel3_IRQn,4);
	NVIC_SetPriority(DMA1_Channel6_IRQn,3);
	
	//####### MPU INIT #########
	
	I2cWrite(MPU_PWR_MGMT_1, MPU_PWR_MGMT_1__DEVICE_RST);
	Wait(100);
	I2cWrite(MPU_PWR_MGMT_1, MPU_PWR_MGMT_1__CLKSEL(1)); //Get MPU out of sleep and set CLK = gyro clock
	Wait(100);
	//I2cWrite(MPU_SIGNAL_PATH_RST, MPU_SIGNAL_PATH_RST__ACCEL_RST | MPU_SIGNAL_PATH_RST__GYRO_RST | MPU_SIGNAL_PATH_RST__TEMP_RST);
	//Wait(100);
	I2cWrite(MPU_CFG, MPU_CFG__DLPF_CFG(1)); // Filter ON (=> Fs=1kHz)
	Wait(100); // wait for filter to settle
	I2cWrite(MPU_SMPLRT_DIV, 2); // Sample rate = Fs/3
	I2cWrite(MPU_INT_EN, MPU_INT_EN__DATA_RDY_EN);
	
	// Calibrate gyro
	gyro_x_dc = 0.0f;
	gyro_y_dc = 0.0f;
	gyro_z_dc = 0.0f;
	
	while (sensor_sample_count < 1000)
	{
		if (FLAG & FLAG_SENSOR)
		{
			FLAG &= ~FLAG_SENSOR;
			
			gyro_x_dc += (float)gyro_x_raw;
			gyro_y_dc += (float)gyro_y_raw;
			gyro_z_dc += (float)gyro_z_raw;
			
			// Blink LED during calibration
			if ((sensor_sample_count & 0x1F) == 0)
				GPIOB->BSRR = GPIO_BSRR_BS_5;
			else if ((sensor_sample_count & 0x1F) == 16)
				GPIOB->BSRR = GPIO_BSRR_BR_5;
		}
		__WFI();
	}
	gyro_x_dc = gyro_x_dc / 1000.0f;
	gyro_y_dc = gyro_y_dc / 1000.0f;
	gyro_z_dc = gyro_z_dc / 1000.0f;
	
	gyro_x_scale = 0.0038f;
	gyro_y_scale = 0.0038f;
	gyro_z_scale = 0.0038f;
	
	//######## MAIN LOOP #########
	
	while (1)
	{
		// Update float registers
		if (FLAG & FLAG_UPDATE_REG)
		{
			FLAG &= ~FLAG_UPDATE_REG;
			
			if (REG_CTRL__LED == 0)
				GPIOB->BSRR = GPIO_BSRR_BS_5;
			else if (REG_CTRL__LED == 1)
				GPIOB->BSRR = GPIO_BSRR_BR_5;
			
			bytes_to_float(&REG_THROTTLE_SCALE, &throttle_scale);
			bytes_to_float(&REG_AILERON_SCALE, &aileron_scale);
			bytes_to_float(&REG_ELEVATOR_SCALE, &elevator_scale);
			bytes_to_float(&REG_RUDDER_SCALE, &rudder_scale);
			
			bytes_to_float(&REG_PITCH_P, &pitch_p);
			bytes_to_float(&REG_PITCH_I, &pitch_i);
			bytes_to_float(&REG_PITCH_D, &pitch_d);
			bytes_to_float(&REG_ROLL_P, &roll_p);
			bytes_to_float(&REG_ROLL_I, &roll_i);
			bytes_to_float(&REG_ROLL_D, &roll_d);
			bytes_to_float(&REG_YAW_P, &yaw_p);
			bytes_to_float(&REG_YAW_I, &yaw_i);
			bytes_to_float(&REG_YAW_D, &yaw_d);
		}
		
		// Process commands
		if (FLAG & FLAG_COMMAND)
		{
			FLAG &= ~FLAG_COMMAND;
			
			if (REG_DEBUG == 3)
			{
				REG_DEBUG = 0;
				uart3_tx_buffer[ 0] = (uint8_t) command_frame_count;
				uart3_tx_buffer[ 1] = (uint8_t) throttle_raw;
				uart3_tx_buffer[ 2] = (uint8_t)(throttle_raw >> 8);
				uart3_tx_buffer[ 3] = (uint8_t) aileron_raw;
				uart3_tx_buffer[ 4] = (uint8_t)(aileron_raw>> 8);
				uart3_tx_buffer[ 5] = (uint8_t) elevator_raw;
				uart3_tx_buffer[ 6] = (uint8_t)(elevator_raw>> 8);
				uart3_tx_buffer[ 7] = (uint8_t) rudder_raw;
				uart3_tx_buffer[ 8] = (uint8_t)(rudder_raw >> 8);
				uart3_tx_buffer[ 9] = (uint8_t) armed_raw;
				uart3_tx_buffer[10] = (uint8_t)(armed_raw >> 8);
				SetDmaUart3Tx(11);
			}
			
			throttle = (float)((int32_t)throttle_raw - (int32_t)REG_CMD_OFFSETS__THROTTLE) * throttle_scale;
			aileron = (float)((int32_t)aileron_raw - (int32_t)REG_CMD_OFFSETS__AIL_ELEV_RUD) * aileron_scale;
			elevator = (float)((int32_t)elevator_raw - (int32_t)REG_CMD_OFFSETS__AIL_ELEV_RUD) * elevator_scale;
			rudder = (float)((int32_t)rudder_raw - (int32_t)REG_CMD_OFFSETS__AIL_ELEV_RUD) * rudder_scale;
			
			if (REG_DEBUG == 4)
			{
				REG_DEBUG = 0;
				uart3_tx_buffer[0] = (uint8_t)command_frame_count;
				float_to_bytes(&throttle, &uart3_tx_buffer[1]);
				float_to_bytes(&aileron, &uart3_tx_buffer[5]);
				float_to_bytes(&elevator, &uart3_tx_buffer[9]);
				float_to_bytes(&rudder, &uart3_tx_buffer[13]);
				SetDmaUart3Tx(17);
			}
		}
		
		// Process sensors
		if (FLAG & FLAG_SENSOR)
		{
			FLAG &= ~FLAG_SENSOR;
			
			if (REG_DEBUG == 1)
			{
				REG_DEBUG = 0;
				uart3_tx_buffer[ 0] = (uint8_t) sensor_sample_count;
				uart3_tx_buffer[ 1] = (uint8_t) gyro_x_raw;
				uart3_tx_buffer[ 2] = (uint8_t)(gyro_x_raw >> 8);
				uart3_tx_buffer[ 3] = (uint8_t) gyro_y_raw;
				uart3_tx_buffer[ 4] = (uint8_t)(gyro_y_raw >> 8);
				uart3_tx_buffer[ 5] = (uint8_t) gyro_z_raw;
				uart3_tx_buffer[ 6] = (uint8_t)(gyro_z_raw >> 8);
				uart3_tx_buffer[ 7] = (uint8_t) accel_x_raw;
				uart3_tx_buffer[ 8] = (uint8_t)(accel_x_raw >> 8);
				uart3_tx_buffer[ 9] = (uint8_t) accel_y_raw;
				uart3_tx_buffer[10] = (uint8_t)(accel_y_raw >> 8);
				uart3_tx_buffer[11] = (uint8_t) accel_z_raw;
				uart3_tx_buffer[12] = (uint8_t)(accel_z_raw >> 8);
				SetDmaUart3Tx(13);
			}
			
			// Remove DC
			gyro_x = ((float)gyro_x_raw - gyro_x_dc) * gyro_x_scale;
			gyro_y = ((float)gyro_y_raw - gyro_y_dc) * gyro_y_scale;
			gyro_z = ((float)gyro_z_raw - gyro_z_dc) * gyro_z_scale;
			
			if (REG_DEBUG == 2)
			{
				REG_DEBUG = 0;
				uart3_tx_buffer[0] = (uint8_t)sensor_sample_count;
				float_to_bytes(&gyro_x, &uart3_tx_buffer[1]);
				float_to_bytes(&gyro_y, &uart3_tx_buffer[5]);
				float_to_bytes(&gyro_z, &uart3_tx_buffer[9]);
				SetDmaUart3Tx(13);
			}
			
			error_pitch_z = error_pitch;
			error_roll_z = error_roll;
			error_yaw_z = error_yaw;
			
			error_pitch = elevator - gyro_x;
			error_roll = aileron - gyro_y;
			error_yaw = rudder - gyro_z;
			
			error_pitch_int += error_pitch;
			error_roll_int += error_roll;
			error_yaw_int += error_yaw;
			
			pitch = error_pitch * pitch_p + error_pitch_int * pitch_i + (error_pitch - error_pitch_z) * pitch_d;
			roll = error_roll * roll_p + error_roll_int * roll_i + (error_roll - error_roll_z) * roll_d;
			yaw = error_yaw * yaw_p + error_yaw_int * yaw_i + (error_yaw - error_yaw_z) * yaw_d;
			
			if (REG_DEBUG == 5)
			{
				REG_DEBUG = 0;
				uart3_tx_buffer[0] = (uint8_t)sensor_sample_count;
				float_to_bytes(&pitch, &uart3_tx_buffer[1]);
				float_to_bytes(&roll, &uart3_tx_buffer[5]);
				float_to_bytes(&yaw, &uart3_tx_buffer[9]);
				SetDmaUart3Tx(13);
			}
			
			motor_1 = throttle + roll + pitch + yaw;
			motor_2 = throttle + roll - pitch - yaw;
			motor_3 = throttle - roll - pitch + yaw;
			motor_4 = throttle - roll + pitch - yaw;
			
			if (REG_DEBUG == 6)
			{
				REG_DEBUG = 0;
				uart3_tx_buffer[0] = (uint8_t)sensor_sample_count;
				float_to_bytes(&motor_1, &uart3_tx_buffer[1]);
				float_to_bytes(&motor_2, &uart3_tx_buffer[5]);
				float_to_bytes(&motor_3, &uart3_tx_buffer[9]);
				float_to_bytes(&motor_4, &uart3_tx_buffer[13]);
				SetDmaUart3Tx(17);
			}
			
			motor_1_clip = (int32_t)motor_1;
			motor_2_clip = (int32_t)motor_2;
			motor_3_clip = (int32_t)motor_3;
			motor_4_clip = (int32_t)motor_4;
			if (motor_1_clip < REG_MOTOR__MIN)
				motor_1_clip = (int32_t)REG_MOTOR__MIN;
			else if (motor_1_clip > REG_MOTOR__MAX)
				motor_1_clip = (int32_t)REG_MOTOR__MAX;
			if (motor_2_clip < REG_MOTOR__MIN)
				motor_2_clip = (int32_t)REG_MOTOR__MIN;
			else if (motor_2_clip > REG_MOTOR__MAX)
				motor_2_clip = (int32_t)REG_MOTOR__MAX;
			if (motor_3_clip < REG_MOTOR__MIN)
				motor_3_clip = (int32_t)REG_MOTOR__MIN;
			else if (motor_3_clip > REG_MOTOR__MAX)
				motor_3_clip = (int32_t)REG_MOTOR__MAX;
			if (motor_4_clip < REG_MOTOR__MIN)
				motor_4_clip = (int32_t)REG_MOTOR__MIN;
			else if (motor_4_clip > REG_MOTOR__MAX)
				motor_4_clip = (int32_t)REG_MOTOR__MAX;
			
			motor_1_raw = (uint16_t)motor_1_clip;
			motor_2_raw = (uint16_t)motor_2_clip;
			motor_3_raw = (uint16_t)motor_3_clip;
			motor_4_raw = (uint16_t)motor_4_clip;
			
			if (REG_DEBUG == 7)
			{
				REG_DEBUG = 0;
				uart3_tx_buffer[ 0] = (uint8_t) sensor_sample_count;
				uart3_tx_buffer[ 1] = (uint8_t) motor_1_raw;
				uart3_tx_buffer[ 2] = (uint8_t)(motor_1_raw >> 8);
				uart3_tx_buffer[ 3] = (uint8_t) motor_2_raw;
				uart3_tx_buffer[ 4] = (uint8_t)(motor_2_raw >> 8);
				uart3_tx_buffer[ 5] = (uint8_t) motor_3_raw;
				uart3_tx_buffer[ 6] = (uint8_t)(motor_3_raw >> 8);
				uart3_tx_buffer[ 7] = (uint8_t) motor_4_raw;
				uart3_tx_buffer[ 8] = (uint8_t)(motor_4_raw >> 8);
				SetDmaUart3Tx(13);
			}
			
			TIM3->CCR1 = (REG_CTRL__MOTOR_SEL == 1) ? (MOTOR_PULSE_MAX - (uint16_t)REG_CTRL__MOTOR_TEST) : (MOTOR_PULSE_MAX - motor_1_raw);
			TIM3->CCR2 = (REG_CTRL__MOTOR_SEL == 2) ? (MOTOR_PULSE_MAX - (uint16_t)REG_CTRL__MOTOR_TEST) : (MOTOR_PULSE_MAX - motor_2_raw);
			TIM3->CCR3 = (REG_CTRL__MOTOR_SEL == 3) ? (MOTOR_PULSE_MAX - (uint16_t)REG_CTRL__MOTOR_TEST) : (MOTOR_PULSE_MAX - motor_3_raw);
			TIM3->CCR4 = (REG_CTRL__MOTOR_SEL == 4) ? (MOTOR_PULSE_MAX - (uint16_t)REG_CTRL__MOTOR_TEST) : (MOTOR_PULSE_MAX - motor_4_raw);
			TIM3->CR1 |= TIM_CR1_CEN;
		}
		
		__WFI();
	}
}

void Reset_Handler()
{
	// Enable FPU
	*FPU_CPACR = (0xF << 20); 
	
	// Reset pipeline
	__DSB();
	__ISB();
	
	main();
}
