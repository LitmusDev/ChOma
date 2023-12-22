#include "PatchFinder_arm64.h"
#include "PatchFinder.h"
#include "arm64.h"

void pf_section_enumerate_arm64_xrefs(PFSection *section, Arm64XrefTypeMask types, void (^xrefBlock)(Arm64XrefType type, uint64_t source, uint64_t target, bool *stop))
{
	bool stop = false;
	for (uint64_t addr = section->vmaddr; addr < (section->vmaddr + section->size) && !stop; addr += 4) {
		uint32_t inst = pf_section_read32(section, addr);
		if ((types & ARM64_XREF_TYPE_MASK_B) || (types & ARM64_XREF_TYPE_MASK_BL)) {
			uint64_t target = 0;
			bool isBl = 0;
			if (arm64_dec_b_l(inst, addr, &target, &isBl) == 0) {
				if (isBl && (types & ARM64_XREF_TYPE_MASK_BL)) {
					xrefBlock(ARM64_XREF_TYPE_BL, addr, target, &stop);
				}
				else if (!isBl && (types & ARM64_XREF_TYPE_MASK_B)) {
					xrefBlock(ARM64_XREF_TYPE_B, addr, target, &stop);
				}
				continue;
			}
		}
		if (types & ARM64_XREF_TYPE_MASK_ADR) {
			uint64_t target = 0;
			bool isAdrp = false;
			arm64_register reg;
			if (arm64_dec_adr_p(inst, addr, &target, &reg, &isAdrp) == 0) {
				if (!isAdrp) {
					xrefBlock(ARM64_XREF_TYPE_ADR, addr, target, &stop);
				}
				continue;
			}
		}
		#define ADRP_SEEK_BACK 8
		if (types & ARM64_XREF_TYPE_MASK_ADRP_ADD) {
			uint16_t addImm = 0;
			arm64_register addDestinationReg;
			arm64_register addSourceReg;
			if (arm64_dec_add_imm(inst, &addDestinationReg, &addSourceReg, &addImm) == 0) {
				uint32_t adrpInst = 0;
				uint32_t adrpMask = 0;
				if (arm64_gen_adr_p(OPT_BOOL(true), OPT_UINT64_NONE, OPT_UINT64_NONE, addSourceReg, &adrpInst, &adrpMask) == 0) {
					uint64_t adrpAddr = pf_section_find_prev_inst(section, addr, ADRP_SEEK_BACK, adrpInst, adrpMask);
					if (adrpAddr != 0) {
						uint64_t adrpTarget = 0;
						arm64_dec_adr_p(pf_section_read32(section, adrpAddr), adrpAddr, &adrpTarget, NULL, NULL);
						xrefBlock(ARM64_XREF_TYPE_ADRP_ADD, addr, adrpTarget + addImm, &stop);
					}
				}
			}
		}
		if (types & ARM64_XREF_TYPE_MASK_ADRP_LDR) {
			arm64_register ldrDestinationReg;
			arm64_register ldrSourceReg;
			uint64_t ldrImm = 0;
			char ldrType = -1;
			if (arm64_dec_ldr_imm(inst, &ldrDestinationReg, &ldrSourceReg, &ldrImm, &ldrType) == 0) {
				uint32_t adrpInst = 0;
				uint32_t adrpMask = 0;
				if (arm64_gen_adr_p(OPT_BOOL(true), OPT_UINT64_NONE, OPT_UINT64_NONE, ldrSourceReg, &adrpInst, &adrpMask) == 0) {
					uint64_t adrpAddr = pf_section_find_prev_inst(section, addr, ADRP_SEEK_BACK, adrpInst, adrpMask);
					if (adrpAddr != 0) {
						// TODO: Check if between adrp and ldr is either an instruction indicating a function start or something overwriting the source register of ldr
						// Due to this inaccuracy, there are some false positives atm
						// Probably applies to the ADRP+ADD and ADRP+STR cases as well
						uint64_t adrpTarget = 0;
						arm64_dec_adr_p(pf_section_read32(section, adrpAddr), adrpAddr, &adrpTarget, NULL, NULL);
						xrefBlock(ARM64_XREF_TYPE_ADRP_LDR, addr, adrpTarget + ldrImm, &stop);
					}
				}
			}
		}
		if (types & ARM64_XREF_TYPE_MASK_ADRP_STR) {
			arm64_register strDestinationReg;
			arm64_register strSourceReg;
			uint64_t strImm = 0;
			char strType = -1;
			if (arm64_dec_str_imm(inst, &strDestinationReg, &strSourceReg, &strImm, &strType) == 0) {
				uint32_t adrpInst = 0;
				uint32_t adrpMask = 0;
				if (arm64_gen_adr_p(OPT_BOOL(true), OPT_UINT64_NONE, OPT_UINT64_NONE, strSourceReg, &adrpInst, &adrpMask) == 0) {
					uint64_t adrpAddr = pf_section_find_prev_inst(section, addr, ADRP_SEEK_BACK, adrpInst, adrpMask);
					if (adrpAddr != 0) {
						uint64_t adrpTarget = 0;
						arm64_dec_adr_p(pf_section_read32(section, adrpAddr), adrpAddr, &adrpTarget, NULL, NULL);
						xrefBlock(ARM64_XREF_TYPE_ADRP_STR, addr, adrpTarget + strImm, &stop);
					}
				}
			}
		}
	}
}