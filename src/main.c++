
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <gmpxx.h>
#include "version.h"

/* Name mangles a VCD name (with "::" or ":" as a seperator) into a
 * Chisel name (with "." as a seperator). */
static const std::string vcd2chisel(const std::string& vcd_name);

/* Converts a binary-encoded string to a decimal-encoded string. */
static const std::string bits2int(const std::string& value_bits);

int main(int argc, const char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "--version") == 0)) {
        printf("vcd2sep " PCONFIGURE_VERSION "\n");
        exit(0);
    }

    if ((argc == 2 && (strcmp(argv[1], "--help") == 0)) || argc != 4) {
        printf("vcd2step <TOP.vcd> <TOP.flo> <TOP.step>: Converts from VCD to Chisel\n"
               "  vcd2step converts a VCD file to a Chisel tester file\n"
               "\n"
               "  --version: Print the version number and exit\n"
               "  --help:    Print this help text and exit\n"
            );
        exit(0);
    }

    /* Open the two files that we were given. */
    libvcd::vcd vcd(argv[1]);
    auto flo = libflo::flo<libflo::node, libflo::operation<libflo::node>>::parse(argv[2]);
    auto step = fopen(argv[3], "w");

    /* Build a map that contains the list of names that will be output
     * to the poke file. */
    std::unordered_map<std::string, bool> should_poke;
    for (const auto& op: flo->operations())
        if (op->op() == libflo::opcode::IN)
            should_poke[vcd2chisel(op->d()->name())] = true;

    /* Read all the way through the VCD file, */
    while (vcd.has_more_cycles()) {
        vcd.step();

        for (const auto& vcd_name: vcd.all_long_names()) {
            auto chisel_name = vcd2chisel(vcd_name);

            /* Things that aren't outputs just get dropped right
             * here. */
            if (should_poke.find(chisel_name) == should_poke.end())
                continue;

            auto value_bits = vcd.long_name_to_bits(vcd_name);

            auto value_int = bits2int(value_bits);

            fprintf(step, "wire_poke %s %s\n",
                    chisel_name.c_str(),
                    value_int.c_str()
                );
        }

        fprintf(step, "step 1\n");
    }

    fprintf(step, "quit\n");
    fclose(step);

    return 0;
}

const std::string vcd2chisel(const std::string& vcd_name)
{
    char buffer[LINE_MAX];
    strncpy(buffer, vcd_name.c_str(), LINE_MAX);

    for (size_t i = 0; i < strlen(buffer); ++i) {
        while (buffer[i] == ':' && buffer[i+1] == ':')
            memmove(buffer + i, buffer + i + 1, strlen(buffer + i));
        if (buffer[i] == ':')
            buffer[i] = '.';
    }

    return buffer;
}

const std::string bits2int(const std::string& value_bits)
{
    if (value_bits.c_str()[0] != 'b') {
        fprintf(stderr, "Non-binary string '%s'\n", value_bits.c_str());
        abort();
    }

    mpz_class gmp(value_bits.c_str() + 1, 2);
    return gmp.get_str(10);
}
