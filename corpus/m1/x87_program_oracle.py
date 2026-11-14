"""Independent exact-rational oracle for the numerical program's binary output."""
from fractions import Fraction
import struct


def x87_output() -> bytes:
    inputs = [(7, 3), (-7, 3), (7, -3), (0, -3), (1, 0), (0, 0),
              (Fraction(1, 2**1074), 2), (Fraction((2**53-1)*2**971), Fraction(1, 2**1074))]
    output = bytearray()
    for precision, pc in ((24, 0), (53, 0x200), (64, 0x300)):
        for rounding in range(4):
            for index, (a, b) in enumerate(inputs):
                control = 0x7f | pc | (rounding << 10)
                sign = (a < 0) ^ (b < 0)
                status = 0x3800
                if index >= 6:
                    status |= 2  # FLD of the binary64 subnormal sets sticky DE.
                if not b:
                    if not a:
                        significand, exponent = 0xc000000000000000, 0xffff
                        status |= 1
                    else:
                        significand, exponent = 1 << 63, 0x7fff | (sign << 15)
                        status |= 4
                elif not a:
                    significand, exponent = 0, sign << 15
                else:
                    value = abs(Fraction(a) / Fraction(b))
                    power = value.numerator.bit_length() - value.denominator.bit_length()
                    scale = Fraction(2**power) if power >= 0 else Fraction(1, 2**-power)
                    if value < scale:
                        power -= 1
                        scale /= 2
                    scaled = value / scale * 2**(precision-1)
                    integer, remainder = divmod(scaled.numerator, scaled.denominator)
                    increment = False
                    if remainder:
                        status |= 0x20
                        if rounding == 0:
                            twice = 2*remainder
                            increment = twice > scaled.denominator or (twice == scaled.denominator and integer % 2 == 1)
                        elif rounding == 1:
                            increment = sign
                        elif rounding == 2:
                            increment = not sign
                    if increment:
                        integer += 1
                        status |= 0x200
                    if integer == 2**precision:
                        integer >>= 1
                        power += 1
                    significand = integer << (64-precision)
                    exponent = power + 16383 | (sign << 15)
                output += struct.pack("<Q4H", significand, exponent, status, control, 0x27f)
    return bytes(output)
