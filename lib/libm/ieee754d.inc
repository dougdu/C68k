; ============================================================
; ieee754d.inc — IEEE 754 double-precision constants and macros
;
; Native IEEE 754 floating-point library for Motorola 68000
;
; Double precision (binary64) uses two 32-bit registers:
;   D0 (or mem+0) = sign(1) + exponent(11) + mantissa_high(20)
;   D1 (or mem+4) = mantissa_low(32)
; Stored big-endian (natural 68000 byte order).
; ============================================================

; --- Double-precision (binary64) bit layout ---
IEEE754_DBL_SIGN_BIT		equ	31		; in high longword
IEEE754_DBL_SIGN_MASK_HI	equ	$80000000
IEEE754_DBL_EXP_MASK_HI	equ	$7FF00000
IEEE754_DBL_EXP_SHIFT		equ	20		; within high longword
IEEE754_DBL_EXP_BIAS		equ	1023
IEEE754_DBL_MANT_MASK_HI	equ	$000FFFFF	; upper 20 bits of mantissa
IEEE754_DBL_MANT_BITS		equ	52
IEEE754_DBL_IMPLIED_BIT_HI	equ	$00100000	; 1 << 20 in high word

; --- Special values (high:low) ---
DBL_ZERO_HI			equ	$00000000
DBL_ZERO_LO			equ	$00000000
DBL_INF_HI			equ	$7FF00000
DBL_INF_LO			equ	$00000000
DBL_NEG_INF_HI			equ	$FFF00000
DBL_NEG_INF_LO			equ	$00000000
DBL_QNAN_HI			equ	$7FFFFFFF
DBL_QNAN_LO			equ	$FFFFFFFF

; --- Common constants (high longword only, low = 0 for exact values) ---
DBL_HALF_HI			equ	$3FE00000	; 0.5
DBL_ONE_HI			equ	$3FF00000	; 1.0
DBL_TWO_HI			equ	$40000000	; 2.0
DBL_TEN_HI			equ	$40240000	; 10.0

; --- Precision limits ---
DBL_DIG				equ	15
DBL_MANT_DIG			equ	53
DBL_MAX_EXP			equ	1024
DBL_MIN_EXP			equ	-1021
DBL_MAX_10_EXP			equ	308
DBL_MIN_10_EXP			equ	-307

; --- Stack parameter offsets for double args (after LINK A6) ---
; double arg1 at 8(a6):12(a6)  (high:low)
; double arg2 at 16(a6):20(a6) (high:low)
DBL_ARG1_HI			equ	8
DBL_ARG1_LO			equ	12
DBL_ARG2_HI			equ	16
DBL_ARG2_LO			equ	20

; --- Extract double exponent from high longword in Dn ---
; Result: Dn = 0..2047 (11-bit biased exponent)
EXTRACT_DBL_EXP	macro	reg
	swap	reg
	lsr.w	#4,reg
	andi.l	#$7FF,reg
	endm

; --- Insert double exponent into high longword ---
; Entry: Dn = 0..2047, result: Dn has exp in bits 30-20
INSERT_DBL_EXP	macro	reg
	andi.l	#$7FF,reg
	lsl.w	#4,reg
	swap	reg
	clr.w	reg
	endm
