#include <cstdio>

#include "../../include/cpu.h"
#include "isa/riscv32.h"
#include "../../include/memory.h"
#include "../../include/watchpoint.h"

CPU_regs cpu = {};   // 初始化为全 0
CpuState cpu_state = CPU_RUNNING;   // 定义（cpu.h 中已声明 enum CpuState 和 extern 变量）

static bool exec_once(uint32_t inst, uint32_t pc);

// 写通用寄存器：x0 恒为 0，任何写入都被忽略
static inline void set_reg(uint32_t idx, uint32_t val) {
    if (idx != 0) cpu.gpr[idx] = val;
}

void cpu_exec(uint64_t n) {
    for (uint64_t i = 0; i < n && cpu_state == CPU_RUNNING; i++) {
        // 1. 取指
        uint32_t instr = paddr_read(cpu.pc, 4);

        // 2. 保存 PC，用于计算 PC+4
        uint32_t this_pc = cpu.pc;
        cpu.pc += 4;

        // 3. 解码并执行
        exec_once(instr, this_pc);

        // 4. 每条指令后检查 watchpoint（联调1 已实现）
        if (wp_check()) {
            cpu_state = CPU_STOPPED;
            break;
        }

        // 5. 强制保持 x0 = 0
        cpu.gpr[0] = 0;
    }
}

// 执行单条指令，返回 false 表示停机
static bool exec_once(uint32_t inst, uint32_t pc) {
    uint32_t op   = OPCODE(inst);
    uint32_t rd   = RD(inst);
    uint32_t rs1  = RS1(inst);
    uint32_t rs2  = RS2(inst);
    uint32_t f3   = FUNCT3(inst);
    uint32_t f7   = FUNCT7(inst);

    uint32_t next_pc = pc + 4;   // 默认顺序执行

    switch (op) {
    // ── LUI ──────────────────────────────────────────────────────────
    case 0x37:
        set_reg(rd, (uint32_t)IMM_U(inst));
        break;

    // ── AUIPC ────────────────────────────────────────────────────────
    case 0x17:
        set_reg(rd, pc + (uint32_t)IMM_U(inst));
        break;

    // ── JAL ──────────────────────────────────────────────────────────
    case 0x6f:
        set_reg(rd, pc + 4);
        next_pc = pc + (uint32_t)IMM_J(inst);
        break;

    // ── JALR ─────────────────────────────────────────────────────────
    case 0x67:
        { uint32_t t = pc + 4;
          next_pc = (cpu.gpr[rs1] + (uint32_t)IMM_I(inst)) & ~1u;
          set_reg(rd, t); }
        break;

    // ── BRANCH ───────────────────────────────────────────────────────
    case 0x63: {
        int32_t  imm    = IMM_B(inst);
        uint32_t src1   = cpu.gpr[rs1];
        uint32_t src2   = cpu.gpr[rs2];
        bool     taken  = false;
        switch (f3) {
            case 0x0: taken = src1 == src2; break;              // BEQ
            case 0x1: taken = src1 != src2; break;              // BNE
            case 0x4: taken = (int32_t)src1 <  (int32_t)src2; break; // BLT
            case 0x5: taken = (int32_t)src1 >= (int32_t)src2; break; // BGE
            case 0x6: taken = src1 <  src2; break;              // BLTU
            case 0x7: taken = src1 >= src2; break;              // BGEU
            default:  goto illegal;
        }
        if (taken) next_pc = pc + (uint32_t)imm;
        break;
    }

    // ── LOAD ─────────────────────────────────────────────────────────
    case 0x03: {
        uint32_t addr = cpu.gpr[rs1] + (uint32_t)IMM_I(inst);
        uint32_t val;
        switch (f3) {
            case 0x0: val = (int32_t)(int8_t) paddr_read(addr, 1); break; // LB
            case 0x1: val = (int32_t)(int16_t)paddr_read(addr, 2); break; // LH
            case 0x2: val =                   paddr_read(addr, 4); break;  // LW
            case 0x4: val =            (uint8_t) paddr_read(addr, 1); break; // LBU
            case 0x5: val =            (uint16_t)paddr_read(addr, 2); break; // LHU
            default:  goto illegal;
        }
        set_reg(rd, val);
        break;
    }

    // ── STORE ────────────────────────────────────────────────────────
    case 0x23: {
        uint32_t addr = cpu.gpr[rs1] + (uint32_t)IMM_S(inst);
        switch (f3) {
            case 0x0: paddr_write(addr, 1, cpu.gpr[rs2]); break; // SB
            case 0x1: paddr_write(addr, 2, cpu.gpr[rs2]); break; // SH
            case 0x2: paddr_write(addr, 4, cpu.gpr[rs2]); break; // SW
            default:  goto illegal;
        }
        break;
    }

    // ── OP-IMM（I 型运算）─────────────────────────────────────────────
    case 0x13: {
        int32_t  imm  = IMM_I(inst);
        uint32_t src  = cpu.gpr[rs1];
        uint32_t shamt = rs2;   // imm[4:0]
        uint32_t res  = 0;
        switch (f3) {
            case 0x0: res = src + (uint32_t)imm;          break; // ADDI
            case 0x2: res = (int32_t)src < imm ? 1 : 0;  break; // SLTI
            case 0x3: res = src < (uint32_t)imm ? 1 : 0; break; // SLTIU
            case 0x4: res = src ^ (uint32_t)imm;          break; // XORI
            case 0x6: res = src | (uint32_t)imm;          break; // ORI
            case 0x7: res = src & (uint32_t)imm;          break; // ANDI
            case 0x1: res = src << shamt;                  break; // SLLI
            case 0x5:
                if (f7 == 0x00) res = src >> shamt;              // SRLI
                else            res = (int32_t)src >> shamt;      // SRAI
                break;
            default: goto illegal;
        }
        set_reg(rd, res);
        break;
    }

    // ── OP（R 型运算）────────────────────────────────────────────────
    case 0x33: {
        uint32_t s1 = cpu.gpr[rs1];
        uint32_t s2 = cpu.gpr[rs2];
        uint32_t res = 0;
        if (f7 == 0x00) {
            switch (f3) {
                case 0x0: res = s1 + s2;                         break; // ADD
                case 0x1: res = s1 << (s2 & 0x1f);              break; // SLL
                case 0x2: res = (int32_t)s1 < (int32_t)s2 ? 1:0; break; // SLT
                case 0x3: res = s1 < s2 ? 1 : 0;                break; // SLTU
                case 0x4: res = s1 ^ s2;                         break; // XOR
                case 0x5: res = s1 >> (s2 & 0x1f);              break; // SRL
                case 0x6: res = s1 | s2;                         break; // OR
                case 0x7: res = s1 & s2;                         break; // AND
                default: goto illegal;
            }
        } else if (f7 == 0x20) {
            switch (f3) {
                case 0x0: res = s1 - s2;                          break; // SUB
                case 0x5: res = (int32_t)s1 >> (s2 & 0x1f);     break; // SRA
                default: goto illegal;
            }
        } else if (f7 == 0x01) {
            // RV32M 扩展
            switch (f3) {
                case 0x0: res = s1 * s2;                                    break; // MUL
                case 0x1: res = (uint32_t)((int64_t)(int32_t)s1*(int32_t)s2 >> 32); break; // MULH
                case 0x2: res = (uint32_t)((int64_t)(int32_t)s1*(uint64_t)s2 >> 32); break; // MULHSU
                case 0x3: res = (uint32_t)((uint64_t)s1*(uint64_t)s2 >> 32);         break; // MULHU
                case 0x4: res = s2 == 0 ? 0xffffffff : (uint32_t)((int32_t)s1/(int32_t)s2); break; // DIV
                case 0x5: res = s2 == 0 ? 0xffffffff : s1/s2;              break; // DIVU
                case 0x6: res = s2 == 0 ? s1 : (uint32_t)((int32_t)s1%(int32_t)s2); break; // REM
                case 0x7: res = s2 == 0 ? s1 : s1%s2;                     break; // REMU
                default: goto illegal;
            }
        } else goto illegal;
        set_reg(rd, res);
        break;
    }

    // ── SYSTEM ───────────────────────────────────────────────────────
    case 0x73:
        if (inst == 0x00100073) {
            // EBREAK：停机（用于测试程序结束）
            cpu_state = CPU_STOPPED;
            return false;
        }
        // ECALL 在 PA4 实现，此处先忽略
        break;

    // ── FENCE（nop 实现即可）──────────────────────────────────────────
    case 0x0f:
        break;

    default:
    illegal:
        printf("非法指令：pc=0x%08x inst=0x%08x\n", pc, inst);
        cpu_state = CPU_ABORT;
        return false;
    }

    cpu.pc = next_pc;
    return true;
}