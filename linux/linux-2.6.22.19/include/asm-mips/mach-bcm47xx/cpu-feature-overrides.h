#ifndef __ASM_MACH_BCM47XX_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_BCM47XX_CPU_FEATURE_OVERRIDES_H

/*
 * Broadcom 4704/4716/4718/4780/5354/5356 based boards
 */

#define cpu_has_tlb			1
#define cpu_has_4kex			1
#define cpu_has_3k_cache		0
#define cpu_has_4k_cache		1
#define cpu_has_tx39_cache		0
#define cpu_has_fpu			0
#define cpu_has_32fpr			0
#define cpu_has_counter			1
/* cpu_has_watch */
#define cpu_has_divec			0
#define cpu_has_vce			0
#define cpu_has_cache_cdex_p		0
#define cpu_has_cache_cdex_s		0
/* cpu_has_prefetch */
/* cpu_has_mcheck */
/* cpu_has_ejtag */
/* cpu_has_llsc	*/

/* cpu_has_mips16 */
#define cpu_has_mdmx			0
#define cpu_has_mips3d			0
#define cpu_has_mmips			0
#define cpu_has_smartmips		0
#define cpu_has_vtag_icache		0
/* cpu_has_dc_aliases */
#define cpu_has_ic_fills_f_dc		0
#define cpu_has_pindexed_dcache		0
#define cpu_icache_snoops_remote_store	0

#define cpu_has_mips32r1		1
#if defined(CONFIG_CPU_MIPS32_R2)
#define cpu_has_mips32r2		1
#endif
#define cpu_has_mips64r1		0
#define cpu_has_mips64r2		0

/* cpu_has_dsp */
#define cpu_has_mipsmt			0
/* cpu_has_userlocal */

#define cpu_has_nofpuex			0
#define cpu_has_64bits			0
#define cpu_has_64bit_zero_reg		0
/* cpu_has_vint */
#define cpu_has_veic			0
#define cpu_has_inclusive_pcaches	0

#define cpu_scache_line_size()		0

#endif /* __ASM_MACH_BCM47XX_CPU_FEATURE_OVERRIDES_H */
