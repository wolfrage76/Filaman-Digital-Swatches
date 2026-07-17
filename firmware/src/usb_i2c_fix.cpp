// CrowPanel routes GT911 touch on GPIO19/20, which overlap USB Serial/JTAG PHY pads.
// Release those pads before Wire runs so I2C touch can communicate reliably.
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"

__attribute__((constructor)) static void crowpanel_release_usb_jtag_phy() {
  SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PAD_PULL_OVERRIDE);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLDOWN);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DM_PULLUP);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DM_PULLDOWN);
}
