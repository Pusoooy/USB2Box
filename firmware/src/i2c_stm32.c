#include "i2c_service.h"
#include "stm32f4xx_hal.h"

typedef struct {
    I2C_HandleTypeDef handle;
    volatile uint16_t result;
    bool slave, slave_read;
    uint8_t receive[I2C_SLAVE_BYTES], transmit[I2C_SLAVE_BYTES];
    i2c_slave_event event;
} i2c_hardware;

static i2c_hardware hardware[2];
static const IRQn_Type ev_irq[] = {I2C1_EV_IRQn, I2C2_EV_IRQn};
static const IRQn_Type er_irq[] = {I2C1_ER_IRQn, I2C2_ER_IRQn};

static i2c_hardware *get(I2C_HandleTypeDef *handle)
{
    return &hardware[handle->Instance == I2C1 ? 0 : 1];
}

static uint32_t pins(unsigned bus)
{
    return bus == 0 ? GPIO_PIN_8 | GPIO_PIN_9 : GPIO_PIN_10 | GPIO_PIN_3;
}

bool i2c_driver_init(unsigned bus, uint32_t speed, bool slave, uint8_t address)
{
    i2c_hardware *hw = &hardware[bus];
    *hw = (i2c_hardware){0};
    hw->slave = slave;
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
    if (HAL_I2C_Init(&hw->handle) != HAL_OK) return false;
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
    if (bus == 0) {
        __HAL_RCC_I2C1_FORCE_RESET();
        __HAL_RCC_I2C1_RELEASE_RESET();
    } else {
        __HAL_RCC_I2C2_FORCE_RESET();
        __HAL_RCC_I2C2_RELEASE_RESET();
    }
    HAL_GPIO_DeInit(GPIOB, pins(bus));
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
        hardware[bus].transmit[i] = i < length ? data[i] : 0xff;
    __set_PRIMASK(mask);
}

void i2c_driver_slave_event(unsigned bus, i2c_slave_event *out)
{
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *out = hardware[bus].event;
    hardware[bus].event.count = 0;
    __set_PRIMASK(mask);
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *handle) { get(handle)->result = STATUS_OK; }
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *handle) { get(handle)->result = STATUS_OK; }

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *handle, uint8_t direction, uint16_t address)
{
    (void)address;
    i2c_hardware *hw = get(handle);
    hw->slave_read = direction == I2C_DIRECTION_RECEIVE;
    if (hw->slave_read)
        HAL_I2C_Slave_Seq_Transmit_IT(handle, hw->transmit, I2C_SLAVE_BYTES, I2C_FIRST_AND_LAST_FRAME);
    else
        HAL_I2C_Slave_Seq_Receive_IT(handle, hw->receive, I2C_SLAVE_BYTES, I2C_FIRST_AND_LAST_FRAME);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *handle)
{
    i2c_hardware *hw = get(handle);
    if (!hw->slave_read && handle->XferCount <= I2C_SLAVE_BYTES) {
        hw->event.count = (uint8_t)(I2C_SLAVE_BYTES - handle->XferCount);
        hw->event.received += hw->event.count;
        for (unsigned i = 0; i < hw->event.count; ++i) hw->event.bytes[i] = hw->receive[i];
    }
    HAL_I2C_EnableListen_IT(handle);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *handle)
{
    i2c_hardware *hw = get(handle);
    hw->result = STATUS_IO;
    ++hw->event.errors;
    if (hw->slave && HAL_I2C_GetState(handle) == HAL_I2C_STATE_READY)
        HAL_I2C_EnableListen_IT(handle);
}

void I2C1_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hardware[0].handle); }
void I2C1_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hardware[0].handle); }
void I2C2_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hardware[1].handle); }
void I2C2_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hardware[1].handle); }
