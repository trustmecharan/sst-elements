#ifndef COUNTER_H_
#define COUNTER_H_

#include <array>
#include <cstdint>
#include <cstdlib>
#include <uint.h>
#include <sint.h>
#include <iostream>
#include <sstream> 
#include <inttypes.h>
#include <string.h>

namespace SST{
#define UNLIKELY(condition) __builtin_expect(static_cast<bool>(condition), 0)


typedef struct Rtlheader {
  FILE* counterout;
  UInt<8> _T;
  UInt<1> clock;
  UInt<1> reset;
  UInt<1> io_inc;
  UInt<4> io_amt;
  UInt<8> io_tot;

  Rtlheader() {
    counterout = fopen("counterout.txt", "w");
    _T.rand_init();
    reset.rand_init();
    io_inc.rand_init();
    io_amt.rand_init();
    io_tot.rand_init();
  }

  UInt<1> io_inc$old;
  UInt<4> io_amt$old;
  UInt<1> reset$old;
  std::array<bool,1> PARTflags;
  bool sim_cached = false;
  bool regs_set = false;
  bool update_registers;
  bool done_reset;
  bool verbose;

  void EVAL_0() {
    PARTflags[0] = false;
    io_tot = _T;
    UInt<8> _T$next;
    if (UNLIKELY(UNLIKELY(reset))) {
      _T$next = UInt<8>(0x0);
    } else {
      UInt<8> _GEN_0;
      if (UNLIKELY(io_inc)) {
        UInt<8> _GEN_1 = io_amt.pad<8>();
        UInt<9> _T_1 = _T + _GEN_1;
        UInt<8> _T_2 = _T_1.tail<1>();
        _GEN_0 = _T_2;
      } else {
        _GEN_0 = _T;
      }
      _T$next = _GEN_0;
    }
    PARTflags[0] |= _T != _T$next;
    if (update_registers) _T = _T$next;
  }

  void eval(bool update_registers, bool verbose, bool done_reset) {
    if (reset || !done_reset) {
      sim_cached = false;
      regs_set = false;
    }
    if (!sim_cached) {
      PARTflags.fill(true);
    }
    sim_cached = regs_set;
    this->update_registers = update_registers;
    this->done_reset = done_reset;
    this->verbose = verbose;
    PARTflags[0] |= io_inc != io_inc$old;
    PARTflags[0] |= io_amt != io_amt$old;
    PARTflags[0] |= reset != reset$old;
    io_inc$old = io_inc;
    io_amt$old = io_amt;
    reset$old = reset;
    if (UNLIKELY(PARTflags[0])) EVAL_0();
    regs_set = true;

    std::stringstream str;
    if(update_registers) {
      str << io_tot;
      fprintf(counterout,"\nio_tot: %s", str.str().c_str());
    }
  }
} Rtlheader;
}
#endif  // COUNTER_H_
