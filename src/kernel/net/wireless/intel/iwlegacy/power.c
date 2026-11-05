/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native 3945 EEPROM interpolation/temperature compensation and gain table,
 * adapted from Linux iwlegacy/3945.c.
 */
#include "internal.h"
static const uint8_t power_gain_table[2][78][2] = {
	{
	 {251, 127},		/* 2.4 GHz, highest power */
	 {251, 127},
	 {251, 127},
	 {251, 127},
	 {251, 125},
	 {251, 110},
	 {251, 105},
	 {251, 98},
	 {187, 125},
	 {187, 115},
	 {187, 108},
	 {187, 99},
	 {243, 119},
	 {243, 111},
	 {243, 105},
	 {243, 97},
	 {243, 92},
	 {211, 106},
	 {211, 100},
	 {179, 120},
	 {179, 113},
	 {179, 107},
	 {147, 125},
	 {147, 119},
	 {147, 112},
	 {147, 106},
	 {147, 101},
	 {147, 97},
	 {147, 91},
	 {115, 107},
	 {235, 121},
	 {235, 115},
	 {235, 109},
	 {203, 127},
	 {203, 121},
	 {203, 115},
	 {203, 108},
	 {203, 102},
	 {203, 96},
	 {203, 92},
	 {171, 110},
	 {171, 104},
	 {171, 98},
	 {139, 116},
	 {227, 125},
	 {227, 119},
	 {227, 113},
	 {227, 107},
	 {227, 101},
	 {227, 96},
	 {195, 113},
	 {195, 106},
	 {195, 102},
	 {195, 95},
	 {163, 113},
	 {163, 106},
	 {163, 102},
	 {163, 95},
	 {131, 113},
	 {131, 106},
	 {131, 102},
	 {131, 95},
	 {99, 113},
	 {99, 106},
	 {99, 102},
	 {99, 95},
	 {67, 113},
	 {67, 106},
	 {67, 102},
	 {67, 95},
	 {35, 113},
	 {35, 106},
	 {35, 102},
	 {35, 95},
	 {3, 113},
	 {3, 106},
	 {3, 102},
	 {3, 95}		/* 2.4 GHz, lowest power */
	},
	{
	 {251, 127},		/* 5.x GHz, highest power */
	 {251, 120},
	 {251, 114},
	 {219, 119},
	 {219, 101},
	 {187, 113},
	 {187, 102},
	 {155, 114},
	 {155, 103},
	 {123, 117},
	 {123, 107},
	 {123, 99},
	 {123, 92},
	 {91, 108},
	 {59, 125},
	 {59, 118},
	 {59, 109},
	 {59, 102},
	 {59, 96},
	 {59, 90},
	 {27, 104},
	 {27, 98},
	 {27, 92},
	 {115, 118},
	 {115, 111},
	 {115, 104},
	 {83, 126},
	 {83, 121},
	 {83, 113},
	 {83, 105},
	 {83, 99},
	 {51, 118},
	 {51, 111},
	 {51, 104},
	 {51, 98},
	 {19, 116},
	 {19, 109},
	 {19, 102},
	 {19, 98},
	 {19, 93},
	 {171, 113},
	 {171, 107},
	 {171, 99},
	 {139, 120},
	 {139, 113},
	 {139, 107},
	 {139, 99},
	 {107, 120},
	 {107, 113},
	 {107, 107},
	 {107, 99},
	 {75, 120},
	 {75, 113},
	 {75, 107},
	 {75, 99},
	 {43, 120},
	 {43, 113},
	 {43, 107},
	 {43, 99},
	 {11, 120},
	 {11, 113},
	 {11, 107},
	 {11, 99},
	 {131, 107},
	 {131, 99},
	 {99, 120},
	 {99, 113},
	 {99, 107},
	 {99, 99},
	 {67, 120},
	 {67, 113},
	 {67, 107},
	 {67, 99},
	 {35, 120},
	 {35, 113},
	 {35, 107},
	 {35, 99},
	 {3, 120}		/* 5.x GHz, lowest power */
	}
};

static int minimum(int a, int b) { return a < b ? a : b; }
static unsigned gain_index(int index) { return index < 0 ? 0 : index > 77 ? 77 : (unsigned)index; }
static int interpolate(const uint8_t* group, int requested, int* result) {
    int power = requested * 2;
    for (unsigned i = 0; i < 5; i++) {
        if (group[i*4] > 77) return IL_BAD_EEPROM;
        if (i && (int8_t)group[(i-1)*4+1] <= (int8_t)group[i*4+1]) return IL_BAD_EEPROM;
    }
    for (unsigned i = 0; i < 5; i++)
        if (power == (int8_t)group[i*4+1]) { *result = group[i*4]; return IL_OK; }
    unsigned left = power > (int8_t)group[5] ? 0 : power > (int8_t)group[9] ? 1 :
        power > (int8_t)group[13] ? 2 : 3;
    int p0 = (int8_t)group[left*4+1], p1 = (int8_t)group[(left+1)*4+1];
    int g0 = group[left*4], g1 = group[(left+1)*4];
    int numerator = (g1-g0)*(power-p0), denominator = p1-p0;
    if (!denominator) return IL_BAD_EEPROM;
    if (denominator < 0) { denominator = -denominator; numerator = -numerator; }
    /* Round to nearest, ties toward +infinity, as upstream's 19-bit fixed
     * point expression does. Bounds above keep all arithmetic in int32. */
    int rounded = numerator >= 0 ? (2*numerator+denominator)/(2*denominator) :
        -((-2*numerator-denominator + 2*denominator-1)/(2*denominator));
    *result = g0 + rounded;
    return IL_OK;
}
int il3945_power_table(const il3945_eeprom_t* e, unsigned band, unsigned channel,
                         int temperature, int user_limit_dbm, uint8_t out[52]) {
    if (!e || !out || (band != 2 && band != 5) || temperature < -260 || temperature > 25 ||
        user_limit_dbm < -10 || user_limit_dbm > 31) return IL_INVALID;
    const il3945_channel_t* ch = 0;
    for (unsigned i = 0; i < IL3945_CHANNEL_COUNT; i++)
        if (e->channels[i].band == band && e->channels[i].number == channel && (e->channels[i].flags & 1)) {
            ch = &e->channels[i]; break;
        }
    if (!ch || ch->max_power_dbm > 31) return IL_INVALID;
    unsigned index = 0;
    if (band == 5) {
        for (index = 1; index < 4; index++) if (channel <= e->calibration[index*64+61]) break;
    }
    const uint8_t* group = e->calibration + index*64;
    int saturation = (int8_t)group[60], factory_temp = (int16_t)il_le16(group+62);
    if (saturation < 40 || factory_temp < -260 || factory_temp > 25) return IL_BAD_EEPROM;
    uint8_t result[52] = {0}; result[0] = band == 2; result[2] = channel;
    result[3] = channel >> 8;
    static const uint8_t rates[12] = {13,15,5,7,9,11,1,3,10,20,55,110};
    int ofdm12_index = 0;
    for (unsigned rate = 0; rate < 12; rate++) {
        int gain;
        if (rate >= 8) gain = ofdm12_index + 10;
        else {
            int clip = saturation / 2;
            if (rate == 5 && band == 5) clip -= 5;
            if (rate == 6) clip -= band == 2 ? 7 : 10;
            if (rate == 7) clip -= band == 2 ? 9 : 12;
            int power = minimum(minimum(ch->max_power_dbm,user_limit_dbm),clip);
            int error = interpolate(group,power,&gain);
            if (error) return error;
            gain += (temperature-factory_temp)*(-11)/100;
            if (rate == 2) ofdm12_index = gain_index(gain);
        }
        unsigned setting = gain_index(gain);
        result[4+rate*4] = rates[rate];
        result[5+rate*4] = power_gain_table[band == 5][setting][0];
        result[6+rate*4] = power_gain_table[band == 5][setting][1];
    }
    for (unsigned i = 0; i < sizeof(result); i++) out[i] = result[i];
    return IL_OK;
}
