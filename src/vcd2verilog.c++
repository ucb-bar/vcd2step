
/*
 * Copyright (C) 2013 Palmer Dabbelt
 *   <palmer@dabbelt.com>
 *
 * This file is part of vcd2step.
 *
 * vcd2step is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vcd2step is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with vcd2step.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libvcd/vcd.h++>
#include <libflo/flo.h++>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <gmpxx.h>
#include "version.h"

typedef libflo::node node;
typedef libflo::operation<node> operation;
typedef libflo::flo<node, operation> flo;

class generic_signal {
public:
    int width;
    std::string name;

    generic_signal(int _width, std::string _name)
        : width(_width), name(_name)
        {}
};

/* Name mangles a VCD name (with "::" or ":" as a seperator) into a
 * Chisel name (with "." as a seperator). */
static const std::string vcd2name(const std::string& vcd_name);

/* Converts a binary-encoded string to a decimal-encoded string. */
static const std::string bits2hex(const std::string& value_bits);

int main(int argc, const char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "--version") == 0)) {
        printf("vcd2sep " PCONFIGURE_VERSION "\n");
        exit(0);
    }

    if ((argc == 2 && (strcmp(argv[1], "--help") == 0)) || argc != 4) {
        printf("vcd2step <TOP.vcd> <TOP.flo> <TOP-Verilog-Dir>: Converts from VCD to Verilog\n"
               "  vcd2step converts a VCD file to a directory of Verilog test files\n"
               "\n"
               "  --version: Print the version number and exit\n"
               "  --help:    Print this help text and exit\n"
            );
        exit(0);
    }

    /* Open the two files that we were given. */
    libvcd::vcd vcd(argv[1]);
    auto flo = flo::parse(argv[2]);
    if (mkdir(argv[3], 0777) != 0) {
        perror("Unable to create output directory");
        abort();
    }

    /* Build a map that contains the list of names that will be output
     * to the poke file. */
    std::unordered_map<std::string, bool> should_poke;
    for (const auto& op: flo->operations()) {
        if (op->op() == libflo::opcode::IN)
            should_poke[vcd2name(op->d()->name())] = true;
    }

    std::unordered_map<std::string, bool> should_poke_or_peek;
    for (const auto& op: flo->operations()) {
        if (op->op() == libflo::opcode::IN)
            should_poke_or_peek[vcd2name(op->d()->name())] = true;
        if (op->op() == libflo::opcode::OUT)
            should_poke_or_peek[vcd2name(op->d()->name())] = true;
    }

    /* Builds a map from Verilog wire names to the file that contains
     * the list of values that signal should take on. */
    std::unordered_map<std::string, FILE *> signal2file;
    std::vector<generic_signal> signals;

    /* The remainder of the circuit can be computed from just its
     * inputs on every cycle.  Those can all be obtained from the VCD
     * alone. */

    /* Read all the way through the VCD file, */
    while (vcd.has_more_cycles()) {
        vcd.step();

        for (const auto& vcd_name: vcd.all_long_names()) {
            auto verilog_name = vcd2name(vcd_name);

            /* Things that aren't outputs just get dropped right
             * here. */
            if (should_poke_or_peek.find(verilog_name) == should_poke_or_peek.end())
                continue;

            auto value_bits = vcd.long_name_to_bits(vcd_name);

#if 0
            fprintf(stderr, "%s: %s\n",
                    verilog_name.c_str(),
                    value_bits.c_str()
                );
#endif

            auto value_int = bits2hex(value_bits);

            auto l = signal2file.find(verilog_name);
            if (l == signal2file.end()) {
                auto file_name = std::string(argv[3]) + "/" + verilog_name + ".dat";

                auto file = fopen(file_name.c_str(), "w");

                signal2file[verilog_name] = file;
                signals.push_back(
                    generic_signal(value_bits.length() - 1,
                                   verilog_name
                        )
                    );

                l = signal2file.find(verilog_name);
                assert(l != signal2file.end());
            }

            auto file = l->second;
            fprintf(file, "%s\n", value_int.c_str());
        }
    }

    /* Proceed to close every file that was opened earlier. */
    for (const auto& signal: signals) {
        auto l = signal2file.find(signal.name);
        if (l == signal2file.end())
            continue;
        fclose(l->second);
    }

    /* Write a Verilog file that contains the test harness that we'll
     * be using to load the test vectors that were just generated. */
    auto v_filename = std::string(argv[3]) + "/" + flo->class_name() + "_vcd2verilog.v";
    auto v = fopen(v_filename.c_str(), "w");

    fprintf(v, "// Auto-Generated by vcd2verilog\n");
    fprintf(v, "module %s_vcd2verilog;\n", flo->class_name().c_str());
    fprintf(v, "  reg clk = 0;\n");

    for (const auto& signal: signals) {
        if (signal.width == 1) {
            fprintf(v, "  reg %s;\n", signal.name.c_str());
        } else {
            fprintf(v, "  reg [%d:0] %s;\n",
                    signal.width - 1,
                    signal.name.c_str()
                );
        }
    }

    for (const auto& signal: signals) {
        if (signal.width == 1) {
            fprintf(v, "  reg __list__%s [0:`CYCLE_MAX];\n", signal.name.c_str());
        } else {
            fprintf(v, "  reg [%d:0] __list__%s [0:`CYCLE_MAX];\n",
                    signal.width - 1,
                    signal.name.c_str()
                );
        }
    }

    fprintf(v,
            "  %s %s\n  (\n",
            flo->class_name().c_str(),
            flo->class_name().c_str()
        );
    for (const auto& signal: signals) {
        fprintf(v, "    .%s (%s),\n",
                signal.name.c_str(),
                signal.name.c_str()
            );
    }
    fprintf(v, "  );\n");


    fprintf(v,
            "  initial begin\n"
            "  end\n"
        );

    fprintf(v, "  initial begin\n");
    fprintf(v, "    //$vcdpluson;\n");

    for (const auto& signal: signals) {
        fprintf(v, "    $readmemh(\"@@TEST_BASE_DIR@@/%s.dat\", %s);\n",
                signal.name.c_str(),
                signal.name.c_str()
            );
    }
    fprintf(v, "  end\n");

    fprintf(v,
            "  reg [31:0] cycle = 0;\n"
            "  reg failed = 0;\n"
            "  always @(posedge clk);\n"
            "    begin\n"
            "      cycle <= cycle + 1;\n"
            "      if (cycle > `CYCLE_MAX)\n"
            "        begin\n"
            "          $display(\"*** PASSED TEST ***\");\n"
            "          //$vcdplusoff;\n"
            "          $finish;\n"
            "        end\n"
            "      else if (failed)\n"
            "        begin\n"
            "          $display(\"*** FAILED TEST ***\");\n"
            "          $finish;\n"
            "        end\n"
            "      end\n"
        );

    fprintf(v,
            "  always @(posedge clk)\n"
            "    begin\n"
            "      reset <= 1'b0;\n"
        );
    for (const auto& signal: signals) {
        if (should_poke.find(signal.name) == should_poke.end())
            continue;

        fprintf(v, "      %s <= __list__%s[cycle];\n",
                signal.name.c_str(),
                signal.name.c_str()
            );
    }
    for (const auto& signal: signals) {
        if (should_poke.find(signal.name) != should_poke.end())
            continue;

        fprintf(v, "      if (%s != __list__%s[cycle])\n",
                signal.name.c_str(),
                signal.name.c_str()
            );
        fprintf(v, "        failed <= 1'b1;\n");
    }
    fprintf(v, "    end\n");

    fprintf(v, "endmodule\n");

    fclose(v);

    return 0;
}

const std::string vcd2name(const std::string& vcd_name)
{
    char buffer[LINE_MAX];
    strncpy(buffer, vcd_name.c_str(), LINE_MAX);

    for (size_t i = 0; i < strlen(buffer); ++i) {
        while (buffer[i] == ':' && buffer[i+1] == ':')
            memmove(buffer + i, buffer + i + 1, strlen(buffer + i));
        if (buffer[i] == ':')
            buffer[i] = '.';
    }

    char *bufferp = strstr(buffer, ".");
    if (bufferp == NULL)
        bufferp = buffer;
    else
        bufferp++;

    for (size_t i = 0; i < strlen(bufferp); ++i)
        if (bufferp[i] == '.')
            bufferp[i] = '_';

    return bufferp;
}

const std::string bits2hex(const std::string& value_bits)
{
    if (value_bits.c_str()[0] != 'b') {
        fprintf(stderr, "Non-binary string '%s'\n", value_bits.c_str());
        abort();
    }

    mpz_class gmp(value_bits.c_str() + 1, 2);
    return gmp.get_str(16);
}
