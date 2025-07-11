#include <stdio.h>

#include "hal/delay.h"
#include "wspr.h"
#include <cstring>
#include <ctype.h>
#include "config.h"
#include <stdlib.h>
#include <time.h>
#include <cmath>

const uint8_t LP_A = 0;
const uint8_t LP_B = 1;
const uint8_t LP_C = 2;
const uint8_t LP_D = 3;

i2c_port *port = &DEFAULT_I2C_PORT;
uint8_t i2c_bus_addr = 0x60;

uint8_t txBuffer[WSPR_SYMBOL_COUNT];

uint8_t *getBuffer()
{
    return txBuffer;
}

uint8_t si5351_write_bulk(uint8_t addr, uint8_t bytes, uint8_t *data)
{
    return i2c_write_bytes(port, i2c_bus_addr, addr, bytes, data);
}

uint8_t si5351_write(uint8_t addr, uint8_t data)
{
    return i2c_write_byte(port, i2c_bus_addr, addr, data);
}

int si5351_read(uint8_t addr)
{
    uint8_t value;

    if (i2c_read_byte(port, i2c_bus_addr, addr, &value) != HAL_OK)
    {
        return -1;
    }

    return value;
}

char letterize(int x)
{
    return (char)x + 65;
}

char *getLocator(double lat, double lon, int size)
{
    static char locator[7];
    double LON_F[] = {20, 2.0, 0.083333, 0.008333, 0.0003472083333333333};
    double LAT_F[] = {10, 1.0, 0.0416665, 0.004166, 0.0001735833333333333};
    int i;
    lon += 180;
    lat += 90;

    size /= 2;
    size *= 2;

    for (i = 0; i < size / 2; i++)
    {
        if (i % 2 == 1)
        {
            locator[i * 2] = (char)(lon / LON_F[i] + '0');
            locator[i * 2 + 1] = (char)(lat / LAT_F[i] + '0');
        }
        else
        {
            locator[i * 2] = letterize((int)(lon / LON_F[i]));
            locator[i * 2 + 1] = letterize((int)(lat / LAT_F[i]));
        }
        lon = fmod(lon, LON_F[i]);
        lat = fmod(lat, LAT_F[i]);
    }
    locator[i * 2] = 0;

    // Serial.println(locator);

    return locator;
}

void setupPLL(uint8_t pll, uint8_t mult, uint32_t num, uint32_t denom)
{
    uint32_t P1;
    uint32_t P2;
    uint32_t P3;

    P1 = (uint32_t)(128 * ((float)num / (float)denom));
    P1 = (uint32_t)(128 * (uint32_t)(mult) + P1 - 512);
    P2 = (uint32_t)(128 * ((float)num / (float)denom));
    P2 = (uint32_t)(128 * num - denom * P2);
    P3 = denom;

    si5351_write(pll + 0, (P3 & 0x0000FF00) >> 8);
    si5351_write(pll + 1, (P3 & 0x000000FF));
    si5351_write(pll + 2, (P1 & 0x00030000) >> 16);
    si5351_write(pll + 3, (P1 & 0x0000FF00) >> 8);
    si5351_write(pll + 4, (P1 & 0x000000FF));
    si5351_write(pll + 5, ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
    si5351_write(pll + 6, (P2 & 0x0000FF00) >> 8);
    si5351_write(pll + 7, (P2 & 0x000000FF));
}

void setupMultisynth(uint8_t synth, uint32_t Divider, uint8_t rDiv)
{
    uint32_t P1;
    uint32_t P2;
    uint32_t P3;

    P1 = 128 * Divider - 512;
    P2 = 0;
    P3 = 1;

    si5351_write(synth + 0, (P3 & 0x0000FF00) >> 8);
    si5351_write(synth + 1, (P3 & 0x000000FF));
    si5351_write(synth + 2, ((P1 & 0x00030000) >> 16) | rDiv);
    si5351_write(synth + 3, (P1 & 0x0000FF00) >> 8);
    si5351_write(synth + 4, (P1 & 0x000000FF));
    si5351_write(synth + 5, ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
    si5351_write(synth + 6, (P2 & 0x0000FF00) >> 8);
    si5351_write(synth + 7, (P2 & 0x000000FF));
}

void setFrequency(uint64_t frequency)
{
    // uint32_t ReferenceFrequency = 25999980;
    uint32_t ReferenceFrequency = 26000000;
    uint8_t OutputDivider = 1;
    uint8_t rDiv = SI_R_DIV_1;

    if (frequency < 100000000ULL)
    {
        OutputDivider = 128;
        rDiv = SI_R_DIV_128;
    }

    uint32_t Divider = 90000000000ULL / (frequency * OutputDivider);

    uint64_t pllFreq = Divider * frequency * OutputDivider;
    uint8_t mult = pllFreq / (ReferenceFrequency * 100UL);

    uint32_t l = pllFreq % (ReferenceFrequency * 100UL);
    float f = l;
    f *= 1048575;
    f /= ReferenceFrequency;
    uint32_t num = f;
    uint32_t denom = 1048575;
    num = num / 100;

    setupPLL(SI_SYNTH_PLL_A, mult, num, denom);

    setupMultisynth(SI_SYNTH_MS_0, Divider, rDiv);

    int32_t freqChange = frequency;

    if (freqChange < 0)
        freqChange *= -1;

    if (freqChange > 100000)
    {
        si5351_write(SI_PLL_RESET, 0xA0);
    }

    si5351_write(SI_CLK0_CONTROL, 0x4F | SI_CLK_SRC_PLL_A);
}

void si5351_disable()
{
    si5351_write(SI_CLK0_CONTROL, 0x80);
}

int si5351_initialize()
{
    int status_reg = si5351_read(SI5351_DEVICE_STATUS);

    if (status_reg == -1)
    {
        return -1;
    }

    if (status_reg >> 7 == 1)
    {
        return -2;
    }

    return 1;
}

void si5351_startup_tone()
{
    uint64_t freq = 14402500000ULL;

    for (int i = 0; i < 50; i++)
    {
        setFrequency(freq + (100ULL * (i * 50)));
        delay_ms(20);
    }

    setFrequency(WSPR_FREQ20m);

    si5351_disable();
}

uint8_t calculatePower(uint8_t dBmIn)
{
    const uint8_t valid_dbm[19] =
        {0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40,
         43, 47, 50, 53, 57, 60};

    uint8_t validateddBmValue = dBmIn;
    if (validateddBmValue > 60)
        validateddBmValue = 60;

    for (uint8_t i = 0; i < 19; i++)
    {
        if (dBmIn >= valid_dbm[i])
            validateddBmValue = valid_dbm[i];
    }

    return validateddBmValue;
}

void convolve(uint8_t *c, uint8_t *s, uint8_t message_size, uint8_t bit_size)
{
    uint32_t reg_0 = 0;
    uint32_t reg_1 = 0;
    uint32_t reg_temp = 0;
    uint8_t input_bit, parity_bit;
    uint8_t bit_count = 0;
    uint8_t i, j, k;

    for (i = 0; i < message_size; i++)
    {
        for (j = 0; j < 8; j++)
        {
            input_bit = (((c[i] << j) & 0x80) == 0x80) ? 1 : 0;

            reg_0 = reg_0 << 1;
            reg_1 = reg_1 << 1;
            reg_0 |= (uint32_t)input_bit;
            reg_1 |= (uint32_t)input_bit;

            reg_temp = reg_0 & 0xf2d05351;
            parity_bit = 0;
            for (k = 0; k < 32; k++)
            {
                parity_bit = parity_bit ^ (reg_temp & 0x01);
                reg_temp = reg_temp >> 1;
            }
            s[bit_count] = parity_bit;
            bit_count++;

            reg_temp = reg_1 & 0xe4613c47;
            parity_bit = 0;
            for (k = 0; k < 32; k++)
            {
                parity_bit = parity_bit ^ (reg_temp & 0x01);
                reg_temp = reg_temp >> 1;
            }
            s[bit_count] = parity_bit;
            bit_count++;
            if (bit_count >= bit_size)
                break;
        }
    }
}

void wspr_interleave(uint8_t *s)
{
    uint8_t d[WSPR_SYMBOL_COUNT];
    uint8_t rev, index_temp, i, j, k;

    i = 0;

    for (j = 0; j < 255; j++)
    {
        index_temp = j;
        rev = 0;

        for (k = 0; k < 8; k++)
        {
            if (index_temp & 0x01)
            {
                rev = rev | (1 << (7 - k));
            }
            index_temp = index_temp >> 1;
        }

        if (rev < WSPR_SYMBOL_COUNT)
        {
            d[rev] = s[i];
            i++;
        }

        if (i >= WSPR_SYMBOL_COUNT)
        {
            break;
        }
    }

    memcpy(s, d, WSPR_SYMBOL_COUNT);
}

void wspr_merge_sync_vector(uint8_t *g)
{
    uint8_t i;
    const uint8_t sync_vector[WSPR_SYMBOL_COUNT] =
        {1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0,
         1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0,
         0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1,
         0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0,
         1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1,
         0, 0, 1, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1,
         1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
         1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0};

    for (i = 0; i < WSPR_SYMBOL_COUNT; i++)
    {
        txBuffer[i] = sync_vector[i] + (2 * g[i]);
    }
}

uint8_t wspr_code(char c)
{
    if (isdigit(c))
    {
        return (uint8_t)(c - 48);
    }
    else if (c == ' ')
    {
        return 36;
    }
    else if (c >= 'A' && c <= 'Z')
    {
        return (uint8_t)(c - 55);
    }
    else
    {
        return 255;
    }
}

uint32_t wspr_call_hash(const char *call)
{
    uint32_t a, b, c;
    char CallWithSuPrefix[11];
    uint8_t Length = strlen(call);

    for (int i = 0; i < 6; i++)
    {
        if (call[i] == ' ')
        {
            Length = i;
            break;
        }
    }

    strcpy(CallWithSuPrefix, call);

#ifdef CONFIG_WSPR_SUFFIX
    if (CONFIG_WSPR_SUFFIX > 0)
    {
        CallWithSuPrefix[Length] = '/';

        if (CONFIG_WSPR_SUFFIX < 36)
        {
            CallWithSuPrefix[Length + 2] = 0;

            if (CONFIG_WSPR_SUFFIX < 10)
            {
                CallWithSuPrefix[Length + 1] = '0' + CONFIG_WSPR_SUFFIX; // Single digit 0-9
            }
            else
            {
                CallWithSuPrefix[Length + 1] = 'A' + (CONFIG_WSPR_SUFFIX - 10); // Single letter A-Z
            }
        }
        else
        {
            uint8_t n = CONFIG_WSPR_SUFFIX - 36; // Double digit 10-99
            uint8_t t = 1;

            while (n > 9)
            {
                ++t;
                n -= 10;
            }

            CallWithSuPrefix[Length + 1] = '0' + t;
            CallWithSuPrefix[Length + 2] = '0' + n;
            CallWithSuPrefix[Length + 3] = 0;
        }
    }
#endif

    Length = strlen(CallWithSuPrefix);

    a = b = c = 0xdeadbeef + Length + 146;

    const uint32_t *k = (const uint32_t *)CallWithSuPrefix;

    switch (Length)
    {
    case 10:
        c += k[2] & 0xffff;
        b += k[1];
        a += k[0];
        break;
    case 9:
        c += k[2] & 0xff;
        b += k[1];
        a += k[0];
        break;
    case 8:
        b += k[1];
        a += k[0];
        break;
    case 7:
        b += k[1] & 0xffffff;
        a += k[0];
        break;
    case 6:
        b += k[1] & 0xffff;
        a += k[0];
        break;
    case 5:
        b += k[1] & 0xff;
        a += k[0];
        break;
    case 4:
        a += k[0];
        break;
    case 3:
        a += k[0] & 0xffffff;
        break;
    }

    c ^= b;
    c -= rot(b, 14);
    a ^= c;
    a -= rot(c, 11);
    b ^= a;
    b -= rot(a, 25);
    c ^= b;
    c -= rot(b, 16);
    a ^= c;
    a -= rot(c, 4);
    b ^= a;
    b -= rot(a, 14);
    c ^= b;
    c -= rot(b, 24);

    c &= 0xFFFF;

    return c;
}

uint8_t encodeChar(char Character)
{
    uint8_t ConvertedNumber;
    if (Character == ' ')
    {
        ConvertedNumber = 36;
    }
    else
    {
        if (isdigit(Character))
        {
            ConvertedNumber = Character - '0';
        }
        else
        {
            ConvertedNumber = 10 + (Character - 'A');
        }
    }
    return ConvertedNumber;
}

void wspr_encode(uint8_t power, int txMode, double latitude, double longitude)
{
    memset(txBuffer, 0, sizeof(txBuffer));

    char callsign[7] = {' ', ' ', ' ', ' ', ' ', ' ', 0};

    for (int i = 0; i < 6; i++)
        callsign[i] = CONFIG_WSPR_CALLSIGN[i];

    char *locator = getLocator(latitude, longitude, 6);

    uint32_t n, m;

    switch (txMode)
    {
    case 3:
        n = wspr_code(locator[1]);
        n = n * 36 + wspr_code(locator[2]);
        n = n * 10 + wspr_code(locator[3]);
        n = n * 27 + (wspr_code(locator[4]) - 10);
        n = n * 27 + (wspr_code(locator[5]) - 10);
        n = n * 27 + (wspr_code(locator[0]) - 10);
        m = 128 * wspr_call_hash(callsign) - power - 1 + 64;
        break;
    case 2:
        n = wspr_code(callsign[0]);
        n = n * 36 + wspr_code(callsign[1]);
        n = n * 10 + wspr_code(callsign[2]);
        n = n * 27 + (wspr_code(callsign[3]) - 10);
        n = n * 27 + (wspr_code(callsign[4]) - 10);
        n = n * 27 + (wspr_code(callsign[5]) - 10);

        m = (27232 + CONFIG_WSPR_SUFFIX);
        m = (m * 128) + power + 2 + 64;
        break;
    default:
        n = wspr_code(callsign[0]);
        n = n * 36 + wspr_code(callsign[1]);
        n = n * 10 + wspr_code(callsign[2]);
        n = n * 27 + (wspr_code(callsign[3]) - 10);
        n = n * 27 + (wspr_code(callsign[4]) - 10);
        n = n * 27 + (wspr_code(callsign[5]) - 10);

        m = ((179 - 10 * (locator[0] - 'A') - (locator[2] - '0')) * 180) + (10 * (locator[1] - 'A')) + (locator[3] - '0');
        m = (m * 128) + power + 64;
        break;
    }

    uint8_t c[11];

    c[3] = (uint8_t)((n & 0x0f) << 4);
    n = n >> 4;
    c[2] = (uint8_t)(n & 0xff);
    n = n >> 8;
    c[1] = (uint8_t)(n & 0xff);
    n = n >> 8;
    c[0] = (uint8_t)(n & 0xff);

    c[6] = (uint8_t)((m & 0x03) << 6);
    m = m >> 2;
    c[5] = (uint8_t)(m & 0xff);
    m = m >> 8;
    c[4] = (uint8_t)(m & 0xff);
    m = m >> 8;
    c[3] |= (uint8_t)(m & 0x0f);
    c[7] = 0;
    c[8] = 0;
    c[9] = 0;
    c[10] = 0;

    uint8_t s[WSPR_SYMBOL_COUNT];
    convolve(c, s, 11, WSPR_SYMBOL_COUNT);

    wspr_interleave(s);

    wspr_merge_sync_vector(s);
}

void si5351_test1()
{
    uint64_t freq = 14402500000ULL;

    for (int i = 0; i < 100; i++)
    {
        setFrequency(freq + (100ULL * (i * 50)));
        delay_ms(20);
    }

    si5351_disable();

    delay_ms(1000);

    for (int i = 0; i < 100; i++)
    {
        setFrequency(freq + (100ULL * (i * 50)));
        delay_ms(20);
    }

    si5351_disable();
}

void si5351_test2()
{
    uint64_t freq = 14402500000ULL;

    for (int i = 0; i < 300; i++)
    {
        setFrequency(freq + (100ULL * (i * 50)));
        delay_ms(20);
    }

    si5351_disable();
}

uint8_t wspr_isInTimeslot(int minute, int second)
{
    // DEBUG ONLY, TESTING ONLY
    // if (minute % 2 == 1 && second > 56)
    // {
    //     return 1;
    // }

    minute++;

    if (minute % 10 == 8) // TX at 8 0
    {
        if (second >= 58)
        {
            return 1;
        }
    }

    if (minute % 10 == 4) // TX at 4 6
    {
        if (second >= 58)
        {
            return 1;
        }
    }

    return 0;
}