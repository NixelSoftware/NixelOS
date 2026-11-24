#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../engine/parser/css_color.h"
static void color(const char *text, unsigned rgb, unsigned alpha) {
    browser_css_color value;
    if (!browser_css_parse_color(text, text + strlen(text), &value) || value.rgb != rgb || value.alpha != alpha) {
        fprintf(stderr, "Failed color: %s\n", text); assert(0);
    }
}
static void invalid(const char *text) {
    browser_css_color value = {123, 45};
    assert(!browser_css_parse_color(text, text + strlen(text), &value));
    assert(value.rgb == 123 && value.alpha == 45);
}
int main(void) {
    color("hwb(0 0% 0%)", 0xff0000, 255);
    color("hwb(120 0% 0% / 50%)", 0x00ff00, 128);
    color("hwb(-120 0% 0%)", 0x0000ff, 255);
    color("hwb(.5turn 0% 0%)", 0x00ffff, 255);
    color("hwb(0 20% 30%)", 0xb33333, 255);
    color("hwb(0 100% 100%)", 0x808080, 255);
    color("color(srgb 1 0 .5 / .25)", 0xff0080, 64);
    color("color(srgb 100% -5% 50% / 50%)", 0xff0080, 128);
    color("color(srgb 2 -1 0)", 0xff0000, 255);
    color("COLOR(sRGB 0 1 0)", 0x00ff00, 255);
    color("color-mix(in srgb, red, blue)", 0x800080, 255);
    color("color-mix(in srgb, 25% red, blue)", 0x4000bf, 255);
    color("color-mix(in srgb, red 25%, blue)", 0x4000bf, 255);
    color("color-mix(in srgb, red, blue 25%)", 0xbf0040, 255);
    color("color-mix(in srgb, red 20%, blue 20%)", 0x800080, 102);
    color("color-mix(in srgb, red 80%, blue 80%)", 0x800080, 255);
    color("color-mix(in srgb, transparent, red)", 0xff0000, 128);
    color("color-mix(in srgb, transparent, transparent)", 0, 0);
    color("color-mix(in srgb, rgb(255, 0, 0), color(srgb 0 0 1))", 0x800080, 255);
    color("color-mix(in srgb, color-mix(in srgb, red, blue), white)", 0xc080c0, 255);
    color("light-dark(hwb(120 0% 0%), color(srgb 0 0 1))", 0x00ff00, 255);
    color("color-mix(in /*comment*/ srgb, red, blue)", 0x800080, 255);
    invalid("hwb(0, 0%, 0%)"); invalid("hwb(0 0 0)");
    invalid("hwb(0 0% 0% /)"); invalid("hwb(0 0% 0%) extra");
    invalid("color(display-p3 1 0 0)"); invalid("color(srgb 1, 0, 0)");
    invalid("color(srgb 1 0)"); invalid("color(srgb 1 0 0 1)");
    invalid("color-mix(in srgb, red 0%, blue 0%)");
    invalid("color-mix(in srgb, red -1%, blue)");
    invalid("color-mix(in srgb, red 101%, blue)");
    invalid("color-mix(in srgb, red, blue, green)");
    invalid("color-mix(in srgb, currentcolor, blue)");
    invalid("color-mix(in oklab, red, blue)");
    invalid("color-mix(in srgb, red 20%junk, blue)");
    puts("PASS HWB, sRGB colors, nested color mixing, alpha and invalid syntax");
    return 0;
}
