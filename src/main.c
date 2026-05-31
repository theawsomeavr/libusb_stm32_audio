#include <stm32f4xx.h>
#include <usb.h>

/********************
 * Audio Stuff
********************/

#define AUDIO_PWM_TOP 1499
#define AUDIO_SAMPLING_RATE 32000
#define AUDIO_STREAM_EPADDR 0x01
#define AUDIO_BUF_SIZE 1024
#define AUDIO_EP_SIZE 192

#define BUF_SIZE AUDIO_BUF_SIZE
#define BUF_MASK (BUF_SIZE-1)
#define EP0_SIZE 64

typedef struct {
    uint16_t r, l;
} AudioSample;

static struct {
    usbd_device usb_dev;
    uint16_t r_head, w_head;
    AudioSample buf[BUF_SIZE];

    uint32_t ep0_buf[EP0_SIZE*4];
    uint32_t divider;
} Global;

static void push_wheel(AudioSample value){
    uint16_t next_pos = (Global.w_head + 1) & BUF_MASK;
    if(next_pos == Global.r_head)return;

    Global.buf[Global.w_head] = value;
    Global.w_head = next_pos;
}

static uint16_t wheel_item_count(){
    return (Global.w_head - Global.r_head) & BUF_MASK;
}

static uint16_t wheel_available_spaces(){
    uint16_t next_pos = (Global.w_head + 1) & BUF_MASK;
    return (Global.r_head - next_pos) & BUF_MASK;
}

static AudioSample pop_wheel(){
    AudioSample data = Global.buf[Global.r_head];

    Global.r_head++;
    Global.r_head &= BUF_MASK;
    return data;
}

static inline int16_t audio_clamp(int16_t sample){
    if(sample < 0) return 0;
    if(sample > AUDIO_PWM_TOP) return AUDIO_PWM_TOP;
    return sample;
}

static void handle_audio_ep(usbd_device *dev, uint8_t event, uint8_t ep){
    uint8_t buf[AUDIO_EP_SIZE];

    // Out endpoint (Machine -> device)
    if(event != usbd_evt_eprx)return;

    if(wheel_available_spaces() < AUDIO_EP_SIZE){
        return;
    }

    int rx_bytes = usbd_ep_read(dev, ep, buf, AUDIO_EP_SIZE);
    if(rx_bytes <= 0){
        return;
    }

    uint32_t num_of_samples = rx_bytes / 4;
    for(int i = 0; i != num_of_samples; i++){
        int16_t *samples = (int16_t *)(buf + i*4);

        int16_t r_chan = audio_clamp((samples[0] + 0x8000) >> 6);
        int16_t l_chan = audio_clamp((samples[1] + 0x8000) >> 6);

        AudioSample res = {
            .r = (uint16_t)(r_chan),
            .l = (uint16_t)(l_chan),
        };
        push_wheel(res);

        Global.divider++;
        if(Global.divider == AUDIO_SAMPLING_RATE/4){
            GPIOC->ODR ^= GPIO_ODR_OD13;
            Global.divider = 0;
        }
    }
}

/********************
 * USB Stuff
********************/

#define LANG_STR 0
#define MANUFACTURE_STR 1
#define PRODUCT_STR 2

static const struct usb_device_descriptor device_desc = {
    .bLength            = sizeof(device_desc),
    .bDescriptorType    = USB_DTYPE_DEVICE,
    .bcdUSB             = VERSION_BCD(2,0,0),
    .bDeviceClass       = USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass    = USB_SUBCLASS_NONE,
    .bDeviceProtocol    = USB_PROTO_NONE,
    .bMaxPacketSize0    = EP0_SIZE,
    .idVendor           = 0xdead,
    .idProduct          = 0xbabe,
    .bcdDevice          = VERSION_BCD(1,0,0),
    .iManufacturer      = MANUFACTURE_STR,
    .iProduct           = PRODUCT_STR,
    .iSerialNumber      = INTSERIALNO_DESCRIPTOR,
    .bNumConfigurations = 1,
};

// Taken from LUFA's LowLevel/AudioOutput config descriptor
static const uint8_t config_desc[] = {
    0x09, 0x02, 0x64, 0x00, 0x02, 0x01, 0x00, 0xc0, 0x32, 0x09,
    0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x09, 0x24, 0x01, 0x00,
    0x01, 0x1e, 0x00, 0x01, 0x01, 0x0c, 0x24, 0x02, 0x01, 0x01, 0x01, 0x00,
    0x02, 0x03, 0x00, 0x00, 0x00, 0x09, 0x24, 0x03, 0x02, 0x01, 0x03, 0x00,
    0x01, 0x00, 0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x09,
    0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00, 0x07, 0x24, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x0b, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x01,

    AUDIO_SAMPLING_RATE & 0xff, (AUDIO_SAMPLING_RATE >> 8) & 0xff, 0x00,

    0x09, 0x05, 0x01, 0x0d,
    AUDIO_EP_SIZE & 0xff, (AUDIO_EP_SIZE >> 8) & 0xff,

    0x01, 0x00, 0x00, 0x07,
    0x25, 0x01, 0x01, 0x00, 0x00, 0x00 
};

static const struct usb_string_descriptor lang_desc     = USB_ARRAY_DESC(USB_LANGID_ENG_US);
static const struct usb_string_descriptor manuf_desc_en = USB_STRING_DESC("Open source USB stack for STM32");
static const struct usb_string_descriptor prod_desc_en  = USB_STRING_DESC("USB audio soundcard");

static usbd_respond desc_req(usbd_ctlreq *req, void **address, uint16_t *length){
    const uint8_t dtype = req->wValue >> 8;
    const uint8_t dnumber = req->wValue & 0xFF;
    const void *desc;
    uint16_t len = 0;

    switch(dtype){
        case USB_DTYPE_DEVICE:
            desc = &device_desc;
            break;
        case USB_DTYPE_CONFIGURATION:
            desc = &config_desc;
            len = sizeof(config_desc);
            break;
        case USB_DTYPE_STRING:
            switch(dnumber){
                case LANG_STR:
                    desc = &lang_desc;
                    break;
                case MANUFACTURE_STR:
                    desc = &manuf_desc_en;
                    break;
                case PRODUCT_STR:
                    desc = &prod_desc_en;
                    break;
                default:
                    return usbd_fail;
            }
            break;

        default:
            return usbd_fail;
    }

    if(len == 0){
        len = ((struct usb_header_descriptor *)desc)->bLength;
    }
    *address = (void *)desc;
    *length = len;
    return usbd_ack;
}

static usbd_respond ep_config_req(usbd_device *dev, uint8_t cfg){
    switch(cfg){
        case 0:
            /* deconfiguring device */
            return usbd_ack;
        case 1:
            /* configuring device */
            usbd_ep_config(dev, AUDIO_STREAM_EPADDR, USB_EPTYPE_ISOCHRONUS, AUDIO_EP_SIZE);
            usbd_reg_endpoint(dev, AUDIO_STREAM_EPADDR, handle_audio_ep);
            return usbd_ack;
        default:
            return usbd_fail;
    }
}

// Control request routine adapted from LUFA's LowLevel/AudioOutput example
static usbd_respond control_req(usbd_device *dev, usbd_ctlreq *req, usbd_rqc_callback *callback){
    switch (req->bRequest) {
        // REQ_SetInterface
        case 11:
        {
            if (req->bmRequestType == (USB_REQ_HOSTTODEV | USB_REQ_STANDARD | USB_REQ_INTERFACE)) {
                dev->status.data_count = 0;
                return usbd_ack;
            }
            break;
        }

        // AUDIO_REQ_SetCurrent
        case 0xff:
            dev->status.data_count = 0;
            return usbd_ack;

        // AUDIO_REQ_SetCurrent
        case 0x01:
        {
            if (req->bmRequestType == (USB_REQ_HOSTTODEV | USB_REQ_CLASS | USB_REQ_ENDPOINT)) {
				/* Extract out the relevant request information to get the target Endpoint address and control being retrieved */
				uint8_t EndpointAddress = (uint8_t)req->wIndex;
				uint8_t EndpointControl = (req->wValue >> 8);

				/* Only handle GET CURRENT requests to the audio endpoint's sample frequency property */
				if ((EndpointAddress == AUDIO_STREAM_EPADDR) && (EndpointControl == 0x01)){
                    dev->status.data_count = 0;
                    return usbd_ack;
				}
            }
            break;
        }

        // AUDIO_REQ_GetCurrent
        case 0x81:
            if (req->bmRequestType == (USB_REQ_DEVTOHOST | USB_REQ_CLASS | USB_REQ_ENDPOINT)) {
				/* Extract out the relevant request information to get the target Endpoint address and control being retrieved */
				uint8_t EndpointAddress = (uint8_t)req->wIndex;
				uint8_t EndpointControl = (req->wValue >> 8);

				/* Only handle GET CURRENT requests to the audio endpoint's sample frequency property */
				if ((EndpointAddress == AUDIO_STREAM_EPADDR) && (EndpointControl == 0x01)){
                    *(uint32_t *)req->data = AUDIO_SAMPLING_RATE;
                    dev->status.data_count = 3;
                    return usbd_ack;
				}
			}
            break;
    }

    return usbd_fail;
}

static void usb_create(){
    usbd_device *dev = &Global.usb_dev;

    usbd_init(dev, &usbd_hw, EP0_SIZE, Global.ep0_buf,
              sizeof(Global.ep0_buf));

    usbd_reg_config(dev, ep_config_req);
    usbd_reg_control(dev, control_req);
    usbd_reg_descr(dev, desc_req);

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // Alternate function for pins PA11 and PA12
    GPIOA->MODER =
        (GPIOA->MODER & ~(GPIO_MODER_MODER11_Msk | GPIO_MODER_MODER12_Msk)) |
        (0b10 << GPIO_MODER_MODER11_Pos) |
        (0b10 << GPIO_MODER_MODER12_Pos);

    GPIOA->AFR[1] =
        (GPIOA->AFR[1] & ~(GPIO_AFRH_AFSEL11_Msk | GPIO_AFRH_AFSEL12_Msk)) |
        (10 << GPIO_AFRH_AFSEL11_Pos) |
        (10 << GPIO_AFRH_AFSEL12_Pos);

    usbd_enable(dev, 1);
    usbd_connect(dev, 1);
}

int main(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                    RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOCEN;

    // Setup timer2 for PWM audio output
    {
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
        RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
        
        //PA1 to alternate function
        GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER1) | (0b10 << GPIO_MODER_MODER1_Pos);
        GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER0) | (0b10 << GPIO_MODER_MODER0_Pos);

        //AF01 for PA1 (TIM2 2)
        GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(GPIO_AFRL_AFSEL1 | GPIO_AFRL_AFSEL0)) |
                        (1 << GPIO_AFRL_AFSEL1_Pos) |
                        (1 << GPIO_AFRL_AFSEL0_Pos);

        TIM2->CCMR1 = 
            TIM_CCMR1_OC1PE | (0b110 << TIM_CCMR1_OC1M_Pos) |
            TIM_CCMR1_OC2PE | (0b110 << TIM_CCMR1_OC2M_Pos);

        TIM2->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
        TIM2->PSC = 0;
        TIM2->ARR = AUDIO_PWM_TOP;
        TIM2->DIER = TIM_DIER_UIE;

        TIM2->CCR1 = 400;
        TIM2->CCR2 = 400;

        NVIC_EnableIRQ(TIM2_IRQn);
        TIM2->CR1 |= TIM_CR1_CEN;
    }

    GPIOC->MODER = (0b01 << GPIO_MODER_MODE13_Pos) |
                   (0b01 << GPIO_MODER_MODE14_Pos);

    usb_create();

    while(1){
        usbd_poll(&Global.usb_dev);
    }
}

void TIM2_IRQHandler(){
    static uint8_t can_read_whole_buf = 0;
    static uint8_t prescalar = 0;

    if(TIM2->SR & TIM_SR_UIF){
        TIM2->SR &= ~TIM_SR_UIF;

        uint16_t items = wheel_item_count();
        uint8_t has_data = 0;

        if(can_read_whole_buf){
            has_data = items != 0;
            // Buffer was emptied, wait for a new half buffer
            if(has_data == 0){
                can_read_whole_buf = 0;
            }
        } else {
            has_data = items > AUDIO_BUF_SIZE/2;
            // Buffer is full enough, read the whole thing
            if(has_data){
                can_read_whole_buf = 1;
            }
        }

        prescalar++;
        if(prescalar == 2){
            prescalar = 0;

            if(has_data){
                AudioSample sample = pop_wheel();
                TIM2->CCR2 = sample.l;
                TIM2->CCR1 = sample.r;
            }
        }
    }
}

// Called from the reset interrupt vector function
void SystemInit(void){
    // Since the cpu clock is set at 96MHz, the flash cannot keep up with this
    // access time, so a delay is requiered
    FLASH->ACR = FLASH_ACR_LATENCY_2WS;

    // Enable the HSE external clock source
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
        __asm volatile("nop");

    typedef enum {
        SysClkSrc_HSI = 0b00,
        SysClkSrc_HSE = 0b01,
        SysClkSrc_PLL = 0b10,
    } SysClkSrc;

    typedef enum {
        PllClkSrc_HSI = 0,
        PllClkSrc_HSE = 1,
    } PllClkSrc;

    typedef enum {
        PllSysDiv_2 = 0b00,
        PllSysDiv_4 = 0b01,
        PllSysDiv_6 = 0b10,
        PllSysDiv_8 = 0b11,
    } PllSysDiv;

    const uint32_t PLL_DIV = 25;
    const uint32_t PLL_MUL = 192;

    const PllSysDiv PLL_DIV_SYS = PllSysDiv_2;
    const uint32_t PLL_DIV_USB = 4;

    // Setup the PLL
    RCC->PLLCFGR = (PLL_DIV << RCC_PLLCFGR_PLLM_Pos) |
        (PLL_MUL << RCC_PLLCFGR_PLLN_Pos) |
        (PLL_DIV_SYS << RCC_PLLCFGR_PLLP_Pos) |
        (PLL_DIV_USB << RCC_PLLCFGR_PLLQ_Pos) |
        (PllClkSrc_HSE << RCC_PLLCFGR_PLLSRC_Pos);

    // Enable the PLL
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY))
        __asm volatile("nop");

    // Setup the Peripheral Clock divisions
    RCC->CFGR = (0b100 << RCC_CFGR_PPRE1_Pos); // APB1 = Sys / 2

    // Select the SysClk source
    RCC->CFGR |= (SysClkSrc_PLL << RCC_CFGR_SW_Pos);
}
