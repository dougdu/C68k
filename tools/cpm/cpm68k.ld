/* cpm68k.ld -- GNU ld link script for c68k CP/M-68K transient programs.
 *
 * Usage:
 *   m68k-elf-ld -T cpm68k.ld -Ttext 0x500 cpm_sys.o ... -o prog.elf
 *   mkdri -b500 -y -o PROG.68K prog.elf
 *
 * -Ttext sets the .text start (the CP/M-68K TPA base, 0x500). .data and .bss
 * follow with no page gap. There is no ENTRY(): a CP/M transient begins at the
 * first byte of .text, so the object that defines _start MUST be linked first.
 * Two PT_LOADs (R+X text, R+W data/bss) let mkdri emit the contiguous .68K
 * image (text+data, then bss as a NOBITS memsz tail).
 */

PHDRS
{
    text PT_LOAD FLAGS(5);  /* PF_R | PF_X */
    data PT_LOAD FLAGS(6);  /* PF_R | PF_W */
}

SECTIONS
{
    .text :
    {
        _text_start = .;
        *(.text.start)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } :text

    . = ALIGN(2);
    _etext = .;

    .data :
    {
        *(.data)
        *(.data.*)
    } :data

    . = ALIGN(2);
    _edata = .;

    .bss :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data

    . = ALIGN(2);
    _end = .;
}
