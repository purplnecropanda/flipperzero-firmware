#include <furi.h>
#include <furi-hal.h>
#include "ibutton_worker_i.h"
#include "ibutton_key_command.h"

void ibutton_worker_mode_idle_start(iButtonWorker* worker);
void ibutton_worker_mode_idle_tick(iButtonWorker* worker);
void ibutton_worker_mode_idle_stop(iButtonWorker* worker);

void ibutton_worker_mode_emulate_start(iButtonWorker* worker);
void ibutton_worker_mode_emulate_tick(iButtonWorker* worker);
void ibutton_worker_mode_emulate_stop(iButtonWorker* worker);

void ibutton_worker_mode_read_start(iButtonWorker* worker);
void ibutton_worker_mode_read_tick(iButtonWorker* worker);
void ibutton_worker_mode_read_stop(iButtonWorker* worker);

void ibutton_worker_mode_write_start(iButtonWorker* worker);
void ibutton_worker_mode_write_tick(iButtonWorker* worker);
void ibutton_worker_mode_write_stop(iButtonWorker* worker);

const iButtonWorkerModeType ibutton_worker_modes[] = {
    {
        .quant = osWaitForever,
        .start = ibutton_worker_mode_idle_start,
        .tick = ibutton_worker_mode_idle_tick,
        .stop = ibutton_worker_mode_idle_stop,
    },
    {
        .quant = 100,
        .start = ibutton_worker_mode_read_start,
        .tick = ibutton_worker_mode_read_tick,
        .stop = ibutton_worker_mode_read_stop,
    },
    {
        .quant = 1000,
        .start = ibutton_worker_mode_write_start,
        .tick = ibutton_worker_mode_write_tick,
        .stop = ibutton_worker_mode_write_stop,
    },
    {
        .quant = 1000,
        .start = ibutton_worker_mode_emulate_start,
        .tick = ibutton_worker_mode_emulate_tick,
        .stop = ibutton_worker_mode_emulate_stop,
    },
};

/*********************** IDLE ***********************/

void ibutton_worker_mode_idle_start(iButtonWorker* worker) {
}

void ibutton_worker_mode_idle_tick(iButtonWorker* worker) {
}

void ibutton_worker_mode_idle_stop(iButtonWorker* worker) {
}

/*********************** READ ***********************/

extern COMP_HandleTypeDef hcomp1;

void ibutton_worker_comparator_callback(void* hcomp, void* context) {
    iButtonWorker* worker = context;

    if(hcomp == &hcomp1) {
        uint32_t current_dwt_value = DWT->CYCCNT;

        pulse_decoder_process_pulse(
            worker->pulse_decoder,
            hal_gpio_get_rfid_in_level(),
            current_dwt_value - worker->last_dwt_value);

        worker->last_dwt_value = current_dwt_value;
    }
}

bool ibutton_worker_read_comparator(iButtonWorker* worker) {
    bool result = false;

    pulse_decoder_reset(worker->pulse_decoder);
    hal_gpio_init(&gpio_rfid_pull, GpioModeOutputOpenDrain, GpioPullNo, GpioSpeedLow);
    hal_gpio_write(&gpio_rfid_pull, false);

    hal_gpio_init(&gpio_rfid_carrier_out, GpioModeOutputOpenDrain, GpioPullNo, GpioSpeedLow);
    hal_gpio_write(&gpio_rfid_carrier_out, false);

    api_interrupt_add(ibutton_worker_comparator_callback, InterruptTypeComparatorTrigger, worker);

    worker->last_dwt_value = DWT->CYCCNT;
    HAL_COMP_Start(&hcomp1);

    // TODO: rework with thread events, "pulse_decoder_get_decoded_index_with_timeout"
    delay(100);
    int32_t decoded_index = pulse_decoder_get_decoded_index(worker->pulse_decoder);
    if(decoded_index >= 0) {
        pulse_decoder_get_data(
            worker->pulse_decoder, decoded_index, worker->key_data, ibutton_key_get_max_size());
    }

    switch(decoded_index) {
    case PulseProtocolCyfral:
        furi_check(worker->key_p != NULL);
        ibutton_key_set_type(worker->key_p, iButtonKeyCyfral);
        ibutton_key_set_data(worker->key_p, worker->key_data, ibutton_key_get_max_size());
        result = true;
        break;
    case PulseProtocolMetakom:
        furi_check(worker->key_p != NULL);
        ibutton_key_set_type(worker->key_p, iButtonKeyMetakom);
        ibutton_key_set_data(worker->key_p, worker->key_data, ibutton_key_get_max_size());
        result = true;
        break;
        break;
    default:
        break;
    }

    HAL_COMP_Stop(&hcomp1);
    api_interrupt_remove(ibutton_worker_comparator_callback, InterruptTypeComparatorTrigger);
    furi_hal_rfid_pins_reset();

    return result;
}

bool ibutton_worker_read_dallas(iButtonWorker* worker) {
    bool result = false;
    onewire_host_start(worker->host);
    delay(100);
    __disable_irq();
    if(onewire_host_search(worker->host, worker->key_data, NORMAL_SEARCH)) {
        onewire_host_reset_search(worker->host);

        // key found, verify
        if(onewire_host_reset(worker->host)) {
            onewire_host_write(worker->host, DS1990_CMD_READ_ROM);
            bool key_valid = true;
            for(uint8_t i = 0; i < ibutton_key_get_max_size(); i++) {
                if(onewire_host_read(worker->host) != worker->key_data[i]) {
                    key_valid = false;
                    break;
                }
            }

            if(key_valid) {
                result = true;

                furi_check(worker->key_p != NULL);
                ibutton_key_set_type(worker->key_p, iButtonKeyDS1990);
                ibutton_key_set_data(worker->key_p, worker->key_data, ibutton_key_get_max_size());
            }
        }
    } else {
        onewire_host_reset_search(worker->host);
    }
    onewire_host_stop(worker->host);
    __enable_irq();
    return result;
}

void ibutton_worker_mode_read_start(iButtonWorker* worker) {
    furi_hal_power_enable_otg();
}

void ibutton_worker_mode_read_tick(iButtonWorker* worker) {
    bool valid = false;
    if(ibutton_worker_read_dallas(worker)) {
        valid = true;
    } else if(ibutton_worker_read_comparator(worker)) {
        valid = true;
    }

    if(valid) {
        if(worker->read_cb != NULL) {
            worker->read_cb(worker->cb_ctx);
        }

        ibutton_worker_switch_mode(worker, iButtonWorkerIdle);
    }
}

void ibutton_worker_mode_read_stop(iButtonWorker* worker) {
    furi_hal_power_disable_otg();
}

/*********************** EMULATE ***********************/
static void onewire_slave_callback(void* context) {
    furi_assert(context);
    iButtonWorker* worker = context;
    if(worker->emulate_cb != NULL) {
        worker->emulate_cb(worker->cb_ctx, true);
    }
}

void ibutton_worker_emulate_dallas_start(iButtonWorker* worker) {
    uint8_t* device_id = onewire_device_get_id_p(worker->device);
    const uint8_t* key_id = ibutton_key_get_data_p(worker->key_p);
    const uint8_t key_size = ibutton_key_get_max_size();
    memcpy(device_id, key_id, key_size);

    onewire_slave_attach(worker->slave, worker->device);
    onewire_slave_start(worker->slave);
    onewire_slave_set_result_callback(worker->slave, onewire_slave_callback, worker);
}

void ibutton_worker_emulate_dallas_stop(iButtonWorker* worker) {
    onewire_slave_stop(worker->slave);
    onewire_slave_detach(worker->slave);
}

void ibutton_worker_mode_emulate_start(iButtonWorker* worker) {
    furi_assert(worker->key_p);
    switch(ibutton_key_get_type(worker->key_p)) {
    case iButtonKeyDS1990:
        ibutton_worker_emulate_dallas_start(worker);
        break;
    case iButtonKeyCyfral:
    case iButtonKeyMetakom:
        break;
    }
}

void ibutton_worker_mode_emulate_tick(iButtonWorker* worker) {
}

void ibutton_worker_mode_emulate_stop(iButtonWorker* worker) {
    furi_assert(worker->key_p);
    switch(ibutton_key_get_type(worker->key_p)) {
    case iButtonKeyDS1990:
        ibutton_worker_emulate_dallas_stop(worker);
        break;
    case iButtonKeyCyfral:
    case iButtonKeyMetakom:
        break;
    }
}

/*********************** WRITE ***********************/

void ibutton_worker_mode_write_start(iButtonWorker* worker) {
    furi_hal_power_enable_otg();
    onewire_host_start(worker->host);
}

void ibutton_worker_mode_write_tick(iButtonWorker* worker) {
    furi_check(worker->key_p != NULL);
    iButtonWriterResult writer_result = ibutton_writer_write(worker->writer, worker->key_p);
    iButtonWorkerWriteResult result;
    switch(writer_result) {
    case iButtonWriterOK:
        result = iButtonWorkerWriteOK;
        break;
    case iButtonWriterSameKey:
        result = iButtonWorkerWriteSameKey;
        break;
    case iButtonWriterNoDetect:
        result = iButtonWorkerWriteNoDetect;
        break;
    case iButtonWriterCannotWrite:
        result = iButtonWorkerWriteCannotWrite;
        break;
    default:
        result = iButtonWorkerWriteNoDetect;
        break;
    }

    if(worker->write_cb != NULL) {
        worker->write_cb(worker->cb_ctx, result);
    }
}

void ibutton_worker_mode_write_stop(iButtonWorker* worker) {
    furi_hal_power_disable_otg();
    onewire_host_stop(worker->host);
}