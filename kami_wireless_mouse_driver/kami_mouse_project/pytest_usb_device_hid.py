# SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: CC0-1.0
import pytest
from pytest_embedded import Dut


@pytest.mark.esp32s3
@pytest.mark.usb_device
def test_usb_device_hid_example(dut: Dut) -> None:
    dut.expect_exact('USB initialization')
    dut.expect_exact('USB initialization DONE')
    dut.expect_exact('USB mb_latch_init')
    dut.expect_exact('USB button_debounce_init')
    dut.expect_exact('USB swheel_init')
    dut.expect_exact('SPI device configured')
    
