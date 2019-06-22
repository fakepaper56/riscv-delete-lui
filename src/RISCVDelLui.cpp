#include "RISCV.h"
#include "RISCVTargetMachine.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"

#include <cstdint>
#include <set>

#define DEBUG_TYPE "deleteLui"

using namespace llvm;

namespace {

struct RISCVDelLui : public MachineFunctionPass {
  static char ID;
  RISCVDelLui() : MachineFunctionPass(ID) {}
  bool runOnMachineFunction(MachineFunction &MBB) override;
  void RISCVDeleteLUI(MachineInstr &LUI, MachineInstr &Addi, unsigned Reg,
                      int16_t Imm);
  bool detectUncessaryLUI(MachineInstr &MI, unsigned &Reg, int16_t &oldLuiImm,
                          int16_t &oldAddiImm);

private:
  MachineRegisterInfo *MRI;
  std::set<MachineInstr *> DeadInstrs;
};
} // namespace

namespace llvm {
FunctionPass *createRISCVDelLui() { return new RISCVDelLui(); }
} // namespace llvm

char RISCVDelLui::ID = 0;
static RegisterPass<RISCVDelLui> X("Delete unecessary lui",
                                   "Delete unecessary lui Pass");

bool RISCVDelLui::detectUncessaryLUI(MachineInstr &MI, unsigned &oldReg,
                                     int16_t &oldLuiImm, int16_t &oldAddiImm) {
  bool Changed = false;
  if (MI.getOpcode() == RISCV::LUI) {
    unsigned LuiDestReg = MI.getOperand(0).getReg();
    if (!MRI->hasOneUse(LuiDestReg))
      return true;

    MachineInstr &Addi = *MRI->use_begin(LuiDestReg)->getParent();
    if (Addi.getOpcode() != RISCV::ADDIW)
      return false;

    int16_t Imm = 0;
    int16_t newLuiImm = MI.getOperand(1).getImm();
    int16_t newAddiImm = Addi.getOperand(2).getImm();

    if ((oldLuiImm == newLuiImm + 1) && oldAddiImm < 0) {
      Imm = newAddiImm - oldAddiImm - 4096;
      if (Imm >= -2048) {
        RISCVDeleteLUI(MI, Addi, oldReg, Imm);
        Changed = true;
      }
    } else if ((oldLuiImm == newLuiImm - 1) && oldAddiImm > 0) {
      Imm = newAddiImm - oldAddiImm + 4096;
      if (Imm <= 2047) {
        RISCVDeleteLUI(MI, Addi, oldReg, Imm);
        Changed = true;
      }
    }

    oldReg = Addi.getOperand(0).getReg();
    oldLuiImm = newLuiImm;
    oldAddiImm = newAddiImm;
  } else if (MI.getOpcode() == RISCV::ADDI &&
             MI.getOperand(1).getReg() == RISCV::X0) {
    oldLuiImm = 0;
    oldReg = MI.getOperand(0).getReg();
    oldAddiImm = MI.getOperand(2).getImm();
  }
  return Changed;
}

void RISCVDelLui::RISCVDeleteLUI(MachineInstr &Lui, MachineInstr &Addi,
                                 unsigned Reg, int16_t Imm) {
  Addi.getOperand(1).setReg(Reg);
  Addi.getOperand(2).setImm(Imm);

  // Delete Lui instruction
  DeadInstrs.insert(&Lui);

  LLVM_DEBUG(dbgs() << "Delet: " << LUI);
}

bool RISCVDelLui::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;

  MRI = &MF.getRegInfo();
  for (MachineBasicBlock &MBB : MF) {
    unsigned Reg;
    int16_t addiImm, luiImm = -1;
    for (MachineInstr &MI : MBB) {
      Changed |= detectUncessaryLUI(MI, Reg, luiImm, addiImm);
    }
  }

  for (auto *MI : DeadInstrs)
    MI->eraseFromParent();

  return Changed;
}
