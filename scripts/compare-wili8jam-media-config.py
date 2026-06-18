#!/usr/bin/env python3
"""Compare Fruit Jam Linux DVI/audio settings with the wili8jam reference."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise SystemExit(f"missing required file: {path}") from None


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"missing {label}: {needle}")


def default_wili8jam_root() -> Path:
    return Path.cwd().parent / "wili8jam"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--wili8jam-root",
        type=Path,
        default=default_wili8jam_root(),
        help="path to a local freewili/wili8jam checkout",
    )
    args = parser.parse_args(argv)

    wili = args.wili8jam_root.resolve()
    wili_cmake = read_text(wili / "CMakeLists.txt")
    wili_main = read_text(wili / "src" / "main.cpp")
    wili_dvi = read_text(wili / "src" / "dvi.c")
    wili_audio_h = read_text(wili / "src" / "audio.h")
    wili_audio = read_text(wili / "src" / "audio.c")
    wili_i2s = read_text(wili / "src" / "audio_i2s.pio")
    wili_readme = read_text(wili / "README.md")

    for needle, label in (
        ("src/dvi.c", "DVI source linked"),
        ("src/audio.c", "audio source linked"),
        ("pico_generate_pio_header(wili8jam ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_i2s.pio)", "I2S PIO header"),
        ("hardware_dma", "DMA library"),
        ("hardware_i2c", "I2C library"),
    ):
        require(wili_cmake, needle, f"wili8jam CMake {label}")
    for needle, label in (
        ("gfx_init();", "graphics init"),
        ("dvi_init(gfx_get_dvi_buffer());", "DVI init"),
        ('printf("DVI: 640x480 output started\\n");', "DVI boot log"),
        ("if (audio_init())", "audio init"),
        ('printf("Audio: I2S + DAC ready\\n");', "audio boot log"),
    ):
        require(wili_main, needle, f"wili8jam main {label}")

    for needle, label in (
        ("#define H_FRONT_PORCH   16", "DVI horizontal front porch"),
        ("#define H_SYNC_WIDTH    96", "DVI horizontal sync"),
        ("#define H_BACK_PORCH    48", "DVI horizontal back porch"),
        ("#define H_ACTIVE_PIXELS 640", "DVI active width"),
        ("#define V_FRONT_PORCH   10", "DVI vertical front porch"),
        ("#define V_SYNC_WIDTH    2", "DVI vertical sync"),
        ("#define V_BACK_PORCH    33", "DVI vertical back porch"),
        ("#define V_ACTIVE_LINES  480", "DVI active height"),
        ("#define HSTX_FIRST_PIN 12", "HSTX first pin"),
        ("#define PIN_CKP  13", "HSTX clock positive"),
        ("#define PIN_D0P  15", "DVI blue positive"),
        ("#define PIN_D1P  17", "DVI green positive"),
        ("#define PIN_D2P  19", "DVI red positive"),
        ("#define FB_WIDTH  128", "DVI framebuffer width"),
        ("#define FB_HEIGHT 128", "DVI framebuffer height"),
        ("#define OUTPUT_SCALING 3", "DVI 3x scaled framebuffer"),
        ("#define H_BORDER ((H_ACTIVE_PIXELS - FB_WIDTH * OUTPUT_SCALING) / 2)", "DVI horizontal border"),
        ("#define V_BORDER ((V_ACTIVE_LINES - FB_HEIGHT * OUTPUT_SCALING) / 2)", "DVI vertical border"),
        ("static uint32_t vactive_pre[VACTIVE_PRE_LEN]", "DVI active pre command list"),
        ("static uint32_t vactive_post[VACTIVE_POST_LEN]", "DVI active post command list"),
        ("static uint32_t vactive_black[VACTIVE_BLACK_LEN]", "DVI black border command list"),
        ("#define DMA_CMD_BUF_WORDS", "DVI DMA command buffer"),
        ("DMA_SIZE_16 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB", "DVI RGB565 halfword DMA"),
        ("4  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB", "DVI RGB565 red expansion"),
        ("5  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB", "DVI RGB565 green expansion"),
        ("29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB", "DVI RGB565 blue rotation"),
        ("OUTPUT_SCALING << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB", "DVI HSTX 3x scaling"),
        ("uint32_t hstx_hz = 126000000;", "126 MHz HSTX clock"),
        ("uint32_t clkdiv = hstx_hz / 25200000;", "DVI HSTX clkdiv"),
        ("for (int i = 12; i <= 19; ++i)", "HSTX pin range"),
        ("GPIO_DRIVE_STRENGTH_4MA", "HSTX drive strength"),
    ):
        require(wili_dvi, needle, f"wili8jam DVI {label}")

    for needle, label in (
        ("#define WAVE_SINE     0", "sine waveform id"),
        ("#define WAVE_SQUARE   1", "square waveform id"),
        ("#define WAVE_SAW      2", "saw waveform id"),
        ("#define WAVE_TRIANGLE 3", "triangle waveform id"),
        ("#define WAVE_NOISE    4", "noise waveform id"),
        ("void audio_tone(int channel, float freq, int duration_ms, int waveform);", "tone waveform API"),
    ):
        require(wili_audio_h, needle, f"wili8jam audio.h {label}")

    for needle, label in (
        ("#define I2S_DIN_PIN   24", "I2S DIN"),
        ("#define I2S_BCLK_PIN  26", "I2S BCLK"),
        ("// WS = BCLK + 1 = 27", "I2S WS"),
        ("#define I2C_SDA_PIN   20", "I2C SDA"),
        ("#define I2C_SCL_PIN   21", "I2C SCL"),
        ("#define CODEC_RESET_PIN 22", "TLV reset"),
        ("#define DAC_I2C_ADDR  0x18", "TLV address"),
        ("#define SAMPLE_RATE   22050", "wili8jam audio mixer sample rate"),
        ("#define AUDIO_PIO     pio1", "audio PIO1"),
        ("#define AUDIO_SM      0", "audio SM0"),
        ("codec_modify_reg(0x04, 0x03, 0x03);", "TLV PLL from BCLK"),
        ("audio_i2s_program_init(AUDIO_PIO, AUDIO_SM, offset, I2S_DIN_PIN, I2S_BCLK_PIN);", "I2S PIO init"),
        ("uint32_t target = 22050 * 64;", "I2S BCLK target"),
        ("case WAVE_SQUARE:", "square waveform synth"),
        ("case WAVE_SAW:", "saw waveform synth"),
        ("case WAVE_TRIANGLE:", "triangle waveform synth"),
        ("case WAVE_NOISE:", "noise waveform synth"),
        ("ch->noise_lfsr = (ch->noise_lfsr >> 1) | (bit << 15);", "noise LFSR"),
        ("if (waveform < 0 || waveform > WAVE_NOISE) waveform = WAVE_SQUARE;", "waveform range check"),
    ):
        require(wili_audio, needle, f"wili8jam audio {label}")
    for needle, label in (
        ("; OUT pin 0:       DIN  (GPIO 24)", "PIO DIN comment"),
        ("; Side-set pin 0:  BCLK (GPIO 26)", "PIO BCLK comment"),
        ("; Side-set pin 1:  WS   (GPIO 27)", "PIO WS comment"),
        ("sm_config_set_out_shift(&c, false, true, 32);", "MSB-first 32-bit autopull"),
        ("sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);", "joined TX FIFO"),
    ):
        require(wili_i2s, needle, f"wili8jam audio PIO {label}")
    for needle, label in (
        ("DVI via HSTX (640x480@60Hz)", "README DVI feature"),
        ("**Audio** | I2S to TLV320DAC3100 DAC", "README audio feature"),
    ):
        require(wili_readme, needle, f"wili8jam README {label}")

    dts = read_text(REPO / "board/adafruit/adafruit_fruit_jam_rp2350/dts/sifive/adafruit_fruit_jam_rp2350.dts")
    config = read_text(REPO / "board/adafruit/adafruit_fruit_jam_rp2350/adafruit_fruit_jam_rp2350.config")
    boot_clocks = read_text(REPO / "package/pico2-bootloader/bootloader/src/clocks.h")
    dvi_patch = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0019-misc-add-fruitjam-dvi-driver.patch")
    dvi_wili_patch = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0120-misc-add-wili8jam-rgb565-dvi-mode.patch")
    audio_patch = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0018-misc-add-fruitjam-audio-clock-driver.patch")
    audio_waveform_patch = read_text(REPO / "board/raspberrypi/raspberrypi-pico2/patches/linux/0122-misc-add-wili8jam-audio-waveforms.patch")
    rtttl = read_text(REPO / "package/fruitjam-utils/src/fruitjam-rtttl.c")
    wavplay = read_text(REPO / "package/fruitjam-utils/src/fruitjam-wavplay.c")
    dvi_helper = read_text(REPO / "package/fruitjam-utils/src/fruitjam-dvi.c")
    fruitjam_be = read_text(REPO / "board/adafruit/adafruit_fruit_jam_rp2350/rootfs_overlay/root/berry/fruitjam.be")

    for needle, label in (
        ("CONFIG_FRUITJAM_DVI=y", "DVI driver"),
        ("CONFIG_I2C_GPIO=y", "GPIO I2C bus"),
    ):
        require(config, needle, f"Linux config {label}")
    for needle, label in (
        ("CONFIG_FRUITJAM_AUDIO_CLOCK=y", "audio clock driver"),
        ("CONFIG_I2C_CHARDEV=y", "I2C userspace access"),
    ):
        require(config, needle, f"Linux config {label}")

    for needle, label in (
        ("adafruit,i2c-gpios = <20 21>", "I2C pin map"),
        ("adafruit,i2s-gpios = <24 25 26 27 23>", "I2S/audio pin map"),
        ("adafruit,dvi-gpios = <12 13 14 15 16 17 18 19>", "DVI pin map"),
        ("sda-gpios = <&gpio0 20 6>", "I2C SDA GPIO20"),
        ("scl-gpios = <&gpio0 21 6>", "I2C SCL GPIO21"),
        ("compatible = \"adafruit,fruit-jam-rp2350-audio-clock\"", "audio node"),
        ("raspberrypi,pio = <1>", "audio PIO1"),
        ("raspberrypi,din-gpio = <24>", "audio DIN GPIO24"),
        ("raspberrypi,mclk-gpio = <25>", "audio MCLK GPIO25"),
        ("raspberrypi,bclk-gpio = <26>", "audio BCLK GPIO26"),
        ("raspberrypi,ws-gpio = <27>", "audio WS GPIO27"),
        ("raspberrypi,mclk-hz = <15000000>", "audio MCLK 15 MHz"),
        ("compatible = \"adafruit,fruit-jam-rp2350-dvi\"", "DVI node"),
        ("interrupts = <10>", "DVI DMA IRQ"),
    ):
        require(dts, needle, f"Linux DTS {label}")
    for needle, label in (
        ("#define CLK_HSTX\t(CLK_SYS / 2)", "126 MHz HSTX from 252 MHz sysclk"),
        ("CLOCKS_HSTX_CTRL_CLK_SYS_AUXSRC", "HSTX clock source"),
        ("CLOCKS_BASE[CLOCKS_HSTX_DIV_REG] = (CLK_SYS / CLK_HSTX)", "HSTX divider"),
    ):
        require(boot_clocks, needle, f"Linux bootloader clocks {label}")

    for needle, label in (
        ("#define FJ_DVI_WIDTH\t\t\t640u", "DVI width"),
        ("#define FJ_DVI_HEIGHT\t\t\t480u", "DVI height"),
        ("#define FJ_DVI_DREQ_HSTX\t\t52u", "HSTX DREQ"),
        ("#define MODE_H_FRONT_PORCH\t\t16u", "DVI horizontal timing"),
        ("#define MODE_V_FRONT_PORCH\t\t10u", "DVI vertical timing"),
        ("RP2350_CLOCKS_DIV_INT(2)", "126 MHz HSTX divider"),
        ("RP2350_GPIO_FUNC_HSTX", "HSTX GPIO function"),
        ("Fruit Jam HSTX DVI registered at %ux%u RGB332", "DVI registration log"),
    ):
        require(dvi_patch, needle, f"Linux DVI patch {label}")
    for needle, label in (
        ("#define FJ_DVI_WILI_FB_WIDTH\t\t128u", "wili framebuffer width"),
        ("#define FJ_DVI_WILI_FB_HEIGHT\t\t128u", "wili framebuffer height"),
        ("#define FJ_DVI_WILI_SCALE\t\t3u", "wili 3x scaling"),
        ("#define FJ_DVI_WILI_H_BORDER", "wili horizontal border"),
        ("#define FJ_DVI_WILI_V_BORDER", "wili vertical border"),
        ("static u16 fj_dvi_wili_framebuf", "wili RGB565 framebuffer"),
        ("static u32 fj_dvi_wili_active_pre[]", "wili active pre commands"),
        ("static u32 fj_dvi_wili_active_post[]", "wili active post commands"),
        ("static u32 fj_dvi_wili_active_black[]", "wili black active line"),
        ("HSTX_EXPAND_TMDS_L2_NBITS(4)", "wili RGB565 red expansion"),
        ("HSTX_EXPAND_TMDS_L1_NBITS(5)", "wili RGB565 green expansion"),
        ("HSTX_EXPAND_TMDS_L0_ROT(29)", "wili RGB565 blue rotation"),
        ("HSTX_EXPAND_SHIFT_ENC_N(FJ_DVI_WILI_SCALE)", "wili HSTX 3x scaling"),
        ("DMA_CTRL_SIZE_HALFWORD", "wili halfword pixel DMA"),
        ("wili-pattern", "wili pattern command"),
        ("wili-show", "wili show command"),
        ("wili-test", "wili test command"),
    ):
        require(dvi_wili_patch, needle, f"Linux DVI wili8jam patch {label}")
    for needle, label in (
        ("#define FJ_AUDIO_SAMPLE_RATE\t\t8000u", "Linux bring-up audio sample rate"),
        ("#define FJ_AUDIO_MCLK_HZ\t\t15000000u", "audio MCLK"),
        ("text commands. PIO1 state machines drive MCLK on GPIO25", "audio path note"),
        ("fj->din_gpio = 24;", "DIN default"),
        ("fj->mclk_gpio = 25;", "MCLK default"),
        ("fj->bclk_gpio = 26;", "BCLK default"),
        ("fj->ws_gpio = 27;", "WS default"),
        ("of_property_read_u32(pdev->dev.of_node, \"raspberrypi,mclk-hz\"", "MCLK DT override"),
        ("Fruit Jam audio clock ready on /dev/fruitjam-audio", "audio registration log"),
    ):
        require(audio_patch, needle, f"Linux audio patch {label}")
    for needle, label in (
        ("#define FJ_AUDIO_WAVE_SINE\t\t0u", "sine waveform id"),
        ("#define FJ_AUDIO_WAVE_SQUARE\t\t1u", "square waveform id"),
        ("#define FJ_AUDIO_WAVE_SAW\t\t2u", "saw waveform id"),
        ("#define FJ_AUDIO_WAVE_TRIANGLE\t\t3u", "triangle waveform id"),
        ("#define FJ_AUDIO_WAVE_NOISE\t\t4u", "noise waveform id"),
        ("tone HZ MS WAVEFORM", "extended tone protocol"),
        ("wave WAVEFORM HZ MS", "wave command protocol"),
        ("fj_audio_parse_waveform", "waveform parser"),
        ("fj_audio_wave_sample", "waveform sample generator"),
        ("phase - phase_step", "wili8jam-style noise phase gate"),
        ("last_waveform", "waveform status"),
    ):
        require(audio_waveform_patch, needle, f"Linux audio waveform patch {label}")
    for source, name in ((rtttl, "fruitjam-rtttl"), (wavplay, "fruitjam-wavplay")):
        for needle, label in (
            ("#define TLV_ADDR 0x18", "TLV address"),
            ("#define PERIPH_RESET_GPIO 22u", "TLV reset GPIO"),
            ("#define AUDIO_SAMPLE_RATE 8000u", "audio helper sample rate"),
            ("open(\"/dev/fruitjam-audio\"", "audio device"),
            ("open(\"/dev/i2c-0\"", "I2C device"),
            ("I2C_SLAVE", "I2C slave ioctl"),
            ("W(0, 0x04, 0x00);", "TLV PLL input MCLK"),
            ("W(0, 0x1b, 0x00);", "TLV I2S format"),
        ):
            require(source, needle, f"Linux {name} {label}")
    for needle, label in (
        ("I2S_WAVE_SINE = 0", "sine waveform id"),
        ("I2S_WAVE_SQUARE = 1", "square waveform id"),
        ("I2S_WAVE_SAW = 2", "saw waveform id"),
        ("I2S_WAVE_TRIANGLE = 3", "triangle waveform id"),
        ("I2S_WAVE_NOISE = 4", "noise waveform id"),
        ("--waveform WAVE", "waveform CLI"),
        ("tone %u %u %s", "waveform tone command"),
    ):
        require(rtttl, needle, f"Linux fruitjam-rtttl {label}")
    for needle, label in (
        ("fruitjam.audio_waveform_args", "Berry waveform args"),
        ("fruitjam.audio_tone_command = def(hz, ms, loud, backend, waveform)", "Berry tone waveform parameter"),
        ("fruitjam.rtttl_command = def(song, loud, backend, waveform)", "Berry RTTTL waveform parameter"),
    ):
        require(fruitjam_be, needle, f"Berry fruitjam module {label}")
    for needle, label in (
        ("#define DVI_DEV \"/dev/fruitjam-dvi\"", "DVI device"),
        ("#define WIDTH 640u", "DVI helper width"),
        ("#define HEIGHT 480u", "DVI helper height"),
        ("dashboard", "dashboard command"),
        ("pattern", "pattern command"),
        ("show", "show command"),
        ("wili-pattern", "wili pattern command"),
        ("wili-show", "wili show command"),
        ("wili-test", "wili test command"),
    ):
        require(dvi_helper, needle, f"Linux DVI helper {label}")

    print(f"wili8jam media config compare: ok ({wili})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
