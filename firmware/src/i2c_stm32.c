#include "i2c_service.h"
#include "stm32f4xx_hal.h"

typedef struct {
    I2C_HandleTypeDef handle;
    volatile uint16_t result;
    bool slave;
    volatile bool relisten;
    uint8_t slave_direction;
    uint8_t receive[I2C_SLAVE_BYTES], transmit[I2C_SLAVE_BYTES];
    uint8_t transmit_next[I2C_SLAVE_BYTES];
} i2c_hardware;

static i2c_hardware hardware[2];
static const IRQn_Type ev_irq[] = {I2C1_EV_IRQn, I2C2_EV_IRQn};
static const IRQn_Type er_irq[] = {I2C1_ER_IRQn, I2C2_ER_IRQn};

static unsigned bus_index(const I2C_HandleTypeDef *handle)
{
    return handle->Instance == I2C1 ? 0 : 1;
}

static i2c_hardware *get(I2C_HandleTypeDef *handle)
{
    return &hardware[bus_index(handle)];
}

static uint32_t pins(unsigned bus)
{
    return bus == 0 ? GPIO_PIN_8 | GPIO_PIN_9 : GPIO_PIN_10 | GPIO_PIN_3;
}

static void reset_peripheral(unsigned bus)
{
    if (bus == 0) {
        __HAL_RCC_I2C1_FORCE_RESET();
        __HAL_RCC_I2C1_RELEASE_RESET();
    } else {
        __HAL_RCC_I2C2_FORCE_RESET();
        __HAL_RCC_I2C2_RELEASE_RESET();
    }
}

bool i2c_driver_init(unsigned bus, uint32_t speed, bool slave, uint8_t address)
{
    i2c_hardware *hw = &hardware[bus];
    *hw = (i2c_hardware){0};
    hw->slave = slave;
    for (unsigned i = 0; i < I2C_SLAVE_BYTES; ++i) hw->transmit_next[i] = 0xff;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    if (bus == 0) {
        __HAL_RCC_I2C1_CLK_ENABLE();
        hw->handle.Instance = I2C1;
    } else {
        __HAL_RCC_I2C2_CLK_ENABLE();
        hw->handle.Instance = I2C2;
    }
    hw->handle.Init = (I2C_InitTypeDef){
        .ClockSpeed = speed, .DutyCycle = I2C_DUTYCYCLE_2,
        .OwnAddress1 = (uint32_t)address << 1, .AddressingMode = I2C_ADDRESSINGMODE_7BIT,
    };
    if (HAL_I2C_Init(&hw->handle) != HAL_OK) {
        i2c_driver_deinit(bus);
        return false;
    }
    GPIO_InitTypeDef pin = {
        .Pin = bus == 0 ? pins(bus) : GPIO_PIN_10,
        .Mode = GPIO_MODE_AF_OD, .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_FREQ_HIGH, .Alternate = GPIO_AF4_I2C1,
    };
    HAL_GPIO_Init(GPIOB, &pin);
    if (bus == 1) {
        pin.Pin = GPIO_PIN_3;
        pin.Alternate = GPIO_AF9_I2C2;
        HAL_GPIO_Init(GPIOB, &pin);
    }
    if (slave && HAL_I2C_EnableListen_IT(&hw->handle) != HAL_OK) {
        i2c_driver_deinit(bus);
        return false;
    }
    HAL_NVIC_ClearPendingIRQ(ev_irq[bus]);
    HAL_NVIC_ClearPendingIRQ(er_irq[bus]);
    HAL_NVIC_SetPriority(ev_irq[bus], 8, 0);
    HAL_NVIC_SetPriority(er_irq[bus], 8, 0);
    HAL_NVIC_EnableIRQ(er_irq[bus]);
    HAL_NVIC_EnableIRQ(ev_irq[bus]);
    return true;
}

void i2c_driver_deinit(unsigned bus)
{
    HAL_NVIC_DisableIRQ(ev_irq[bus]);
    HAL_NVIC_DisableIRQ(er_irq[bus]);
    HAL_I2C_DeInit(&hardware[bus].handle);
    reset_peripheral(bus);
    HAL_GPIO_DeInit(GPIOB, pins(bus));
}

void i2c_driver_recovery_begin(unsigned bus)
{
    i2c_driver_deinit(bus);
    /* Set the output latches high before selecting open-drain GPIO. */
    GPIOB->BSRR = pins(bus);
    GPIO_InitTypeDef output = {
        .Pin = pins(bus), .Mode = GPIO_MODE_OUTPUT_OD, .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(GPIOB, &output);
}

void i2c_driver_recovery_drive(unsigned bus, bool release_scl, bool release_sda)
{
    const uint32_t scl = bus == 0 ? GPIO_PIN_8 : GPIO_PIN_10;
    const uint32_t sda = bus == 0 ? GPIO_PIN_9 : GPIO_PIN_3;
    GPIOB->BSRR = (release_scl ? scl : scl << 16) | (release_sda ? sda : sda << 16);
}

uint8_t i2c_driver_lines(unsigned bus)
{
    const uint32_t scl = bus == 0 ? GPIO_PIN_8 : GPIO_PIN_10;
    const uint32_t sda = bus == 0 ? GPIO_PIN_9 : GPIO_PIN_3;
    const uint32_t input = GPIOB->IDR;
    return (uint8_t)((input & scl ? I2C_LINE_SCL : 0) | (input & sda ? I2C_LINE_SDA : 0));
}

bool i2c_driver_start(unsigned bus, uint8_t address, bool read, uint8_t *buffer,
                      uint8_t length, bool first, bool last)
{
    i2c_hardware *hw = &hardware[bus];
    const uint32_t frame = first ? (last ? I2C_FIRST_AND_LAST_FRAME : I2C_FIRST_FRAME)
                                 : (last ? I2C_OTHER_AND_LAST_FRAME : I2C_OTHER_FRAME);
    hw->result = JOB_PENDING;
    if (first && __HAL_I2C_GET_FLAG(&hw->handle, I2C_FLAG_BUSY)) return false;
    const uint16_t addr = (uint16_t)address << 1;
    return (read ? HAL_I2C_Master_Seq_Receive_IT(&hw->handle, addr, buffer, length, frame)
                 : HAL_I2C_Master_Seq_Transmit_IT(&hw->handle, addr, buffer, length, frame)) == HAL_OK;
}

uint16_t i2c_driver_result(unsigned bus) { return hardware[bus].result; }

void i2c_driver_slave_tx(unsigned bus, const uint8_t *data, uint8_t length)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    for (unsigned i = 0; i < I2C_SLAVE_BYTES; ++i)
        hardware[bus].transmit_next[i] = i < length ? data[i] : 0xff;
    __set_PRIMASK(mask);
}

void i2c_driver_service(unsigned bus)
{
    i2c_hardware *hw = &hardware[bus];
    if (!hw->relisten) return;
    HAL_NVIC_DisableIRQ(ev_irq[bus]);
    HAL_NVIC_DisableIRQ(er_irq[bus]);
    hw->relisten = false;
    HAL_I2C_DeInit(&hw->handle);
    reset_peripheral(bus);
    const bool restored = HAL_I2C_Init(&hw->handle) == HAL_OK &&
                          HAL_I2C_EnableListen_IT(&hw->handle) == HAL_OK;
    if (!restored) i2c_record_slave_event(bus, I2C_EVENT_ERROR, 0, 0, HAL_GetTick());
    HAL_NVIC_ClearPendingIRQ(ev_irq[bus]);
    HAL_NVIC_ClearPendingIRQ(er_irq[bus]);
    HAL_NVIC_EnableIRQ(er_irq[bus]);
    HAL_NVIC_EnableIRQ(ev_irq[bus]);
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *handle) { get(handle)->result = STATUS_OK; }
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *handle) { get(handle)->result = STATUS_OK; }

static void finish_slave(I2C_HandleTypeDef *handle)
{
    i2c_hardware *hw = get(handle);
    if (!hw->slave_direction) return;
    const uint8_t count = handle->XferCount <= I2C_SLAVE_BYTES ?
                          (uint8_t)(I2C_SLAVE_BYTES - handle->XferCount) : 0;
    i2c_record_slave_event(bus_index(handle), hw->slave_direction, hw->receive, count, HAL_GetTick());
    hw->slave_direction = 0;
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *handle, uint8_t direction, uint16_t address)
{
    (void)address;
    i2c_hardware *hw = get(handle);
    if (hw->slave_direction == I2C_EVENT_RX) {
        /* ADDR has priority over RXNE in HAL. Drain any bytes from the
         * preceding write before HAL installs the repeated-START buffer. */
        for (unsigned i = 0; i < 2 && handle->XferCount &&
             __HAL_I2C_GET_FLAG(handle, I2C_FLAG_RXNE); ++i) {
            hw->receive[I2C_SLAVE_BYTES - handle->XferCount] = (uint8_t)handle->Instance->DR;
            --handle->XferCount;
        }
    }
    finish_slave(handle);
    const bool read = direction == I2C_DIRECTION_RECEIVE;
    HAL_StatusTypeDef status;
    if (read) {
        for (unsigned i = 0; i < I2C_SLAVE_BYTES; ++i) hw->transmit[i] = hw->transmit_next[i];
        status = HAL_I2C_Slave_Seq_Transmit_IT(handle, hw->transmit, I2C_SLAVE_BYTES,
                                               I2C_FIRST_AND_LAST_FRAME);
    } else {
        status = HAL_I2C_Slave_Seq_Receive_IT(handle, hw->receive, I2C_SLAVE_BYTES,
                                              I2C_FIRST_AND_LAST_FRAME);
    }
    if (status == HAL_OK) {
        hw->slave_direction = read ? I2C_EVENT_TX : I2C_EVENT_RX;
    } else {
        __HAL_I2C_CLEAR_ADDRFLAG(handle);
        i2c_record_slave_event(bus_index(handle), I2C_EVENT_ERROR, 0, 0, HAL_GetTick());
        hw->relisten = true;
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *handle)
{
    /* NACK additional data until the next address/STOP. */
    CLEAR_BIT(handle->Instance->CR1, I2C_CR1_ACK);
    finish_slave(handle);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *handle)
{
    finish_slave(handle);
    if (HAL_I2C_EnableListen_IT(handle) != HAL_OK) get(handle)->relisten = true;
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *handle)
{
    i2c_hardware *hw = get(handle);
    if (!hw->slave) {
        hw->result = STATUS_IO;
        return;
    }
    finish_slave(handle);
    /* AF alone is the STM32F4 HAL signal for a master NACK or short STOP. */
    if (HAL_I2C_GetError(handle) != HAL_I2C_ERROR_AF) {
        i2c_record_slave_event(bus_index(handle), I2C_EVENT_ERROR, 0, 0, HAL_GetTick());
        hw->relisten = true;
    }
}

void I2C1_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hardware[0].handle); }
void I2C1_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hardware[0].handle); }
void I2C2_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hardware[1].handle); }
void I2C2_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hardware[1].handle); }
