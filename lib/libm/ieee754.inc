; ============================================================
; ieee754.inc — IEEE 754 single-precision constants and macros
;
; Native IEEE 754 floating-point library for Motorola 68000
; ============================================================

; --- Single-precision (binary32) bit layout ---
IEEE754_SNG_SIGN_BIT		equ	31
IEEE754_SNG_SIGN_MASK		equ	$80000000
IEEE754_SNG_EXP_MASK		equ	$7F800000
IEEE754_SNG_EXP_SHIFT		equ	23
IEEE754_SNG_EXP_BIAS		equ	127
IEEE754_SNG_MANT_MASK		equ	$007FFFFF
IEEE754_SNG_MANT_BITS		equ	23
IEEE754_SNG_IMPLIED_BIT		equ	$00800000	; 1 << 23

; --- Special values ---
SNG_ZERO			equ	$00000000	; +0.0
SNG_NEG_ZERO			equ	$80000000	; -0.0
SNG_INF				equ	$7F800000	; +infinity
SNG_NEG_INF			equ	$FF800000	; -infinity
SNG_QNAN			equ	$7FFFFFFF	; quiet NaN

; --- Common constants ---
SNG_HALF			equ	$3F000000	; 0.5
SNG_ONE				equ	$3F800000	; 1.0
SNG_TWO				equ	$40000000	; 2.0
SNG_TEN				equ	$41200000	; 10.0
SNG_PI				equ	$40490FDB	; 3.14159265
SNG_PI_2			equ	$3FC90FDB	; pi/2
SNG_PI_4			equ	$3F490FDB	; pi/4
SNG_LN2				equ	$3F317218	; ln(2) = 0.693147...
SNG_LOG2E			equ	$3FB8AA3B	; log2(e) = 1.442695...
SNG_INV_LN2			equ	$3FB8AA3B	; 1/ln(2) = log2(e)

; --- Precision limits ---
FLT_DIG				equ	6
FLT_MANT_DIG			equ	24
FLT_MAX_EXP			equ	128
FLT_MIN_EXP			equ	-125
FLT_MAX_10_EXP			equ	38
FLT_MIN_10_EXP			equ	-37

; --- ASCII ---
CR				equ	13
LF				equ	10

; --- Conversion buffer ---
CVTBUFSIZE			equ	349

; --- Shift helpers for 68000 (immediate shifts limited to 1-8) ---
; Extract exponent: swap high word down, shift right 7, mask to 8 bits
; Usage: EXTRACT_EXP Dn  (clobbers Dn, result in Dn.l = 0..255)
EXTRACT_EXP	macro	reg
	swap	reg
	lsr.w	#7,reg
	andi.l	#$FF,reg
	endm

; Insert exponent: shift left 7, swap into high word
; Usage: INSERT_EXP Dn  (Dn.l = 0..255 on entry, shifted to bits 30-23)
INSERT_EXP	macro	reg
	andi.l	#$FF,reg
	lsl.w	#7,reg
	swap	reg
	clr.w	reg
	endm

; --- Reserved IEEE-environment ABI (see core/fenv.a68, IEEE754_REFERENCE.md) ---
; The arithmetic ops do gradual underflow (D3) but do NOT raise these flags yet
; (Conformance-Delta D1) and only round-to-nearest-even is implemented (D4); the
; status word and the _fe_* accessors are RESERVED so flag / directed-rounding
; support can land additively without changing any binding signature.

; Sticky exception flags (canonical bit values for THIS library; IEEE 754 order)
FE_INVALID			equ	$01
FE_DIVBYZERO			equ	$02
FE_OVERFLOW			equ	$04
FE_UNDERFLOW			equ	$08
FE_INEXACT			equ	$10
FE_ALL_EXCEPT			equ	$1F

; Rounding modes.  Only FE_TONEAREST is implemented; _fe_setround rejects the
; rest (returns nonzero) until directed rounding lands (D4).
FE_TONEAREST			equ	0
FE_TOWARDZERO			equ	1
FE_UPWARD			equ	2
FE_DOWNWARD			equ	3

; Feature-query IDs for _fe_supports (a la FORTRAN IEEE_SUPPORT_*): returns 1
; if the capability is real TODAY, else 0 (reserved for a later release).
IEEE_SUPPORT_DATATYPE		equ	1	; binary32 + binary64 arithmetic  -> 1
IEEE_SUPPORT_NAN		equ	2	; quiet NaN                       -> 1
IEEE_SUPPORT_INF		equ	3	; signed infinity                -> 1
IEEE_SUPPORT_SQRT		equ	4	; correctly-rounded sqrt         -> 1
IEEE_SUPPORT_FMA		equ	5	; correctly-rounded fused mul-add-> 1
IEEE_SUPPORT_SUBNORMAL		equ	6	; gradual underflow (D3)         -> 1
IEEE_SUPPORT_FLAG		equ	7	; ops raise sticky flags (D1)    -> 0
IEEE_SUPPORT_HALTING		equ	8	; trapping on exceptions (D2)    -> 0
IEEE_SUPPORT_ROUNDING		equ	9	; directed rounding modes (D4)   -> 0
