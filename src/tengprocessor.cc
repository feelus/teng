/*
 * Teng -- a general purpose templating engine.
 * Copyright (C) 2004  Seznam.cz, a.s.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Seznam.cz, a.s.
 * Naskove 1, Praha 5, 15000, Czech Republic
 * http://www.seznam.cz, mailto:teng@firma.seznam.cz
 *
 *
 * $Id: tengprocessor.cc,v 1.3 2004-09-06 13:10:27 vasek Exp $
 *
 * DESCRIPTION
 * Teng processor. Executes programs.
 *
 * AUTHORS
 * Jan Nemec <jan.nemec@firma.seznam.cz>
 * Vaclav Blazek <blazek@firma.seznam.cz>
 *
 * HISTORY
 * 2003-09-22  (jan)
 *             Created.
 * 2004-05-30  (vasek)
 *             Revised processor source code.
 */

#include <stack>
#include <vector>
#include <map>
#include <algorithm>

#include <fenv.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include "tengerror.h"
#include "tengfunction.h"
#include "tengprocessor.h"
#include "tengfragmentstack.h"

using namespace std;

using namespace Teng;

void Processor_t::Logger_t::logError(Error_t::Level_t level,
                                     const string &message)
{
    if (instr) processor.logErr(*instr, message, level);
}

struct ToLower_t {
    int operator () (int c) const {
        return tolower(c);
    }
};

Processor_t::
FunctionParam_t::FunctionParam_t(Processor_t &processor,
                                 const string &encoding,
                                 const ContentType_t &escaper)
    : encoding(encoding), escaper(escaper), logger(processor)
{
    transform(this->encoding.begin(), this->encoding.end(),
              this->encoding.begin(), ToLower_t());
}

void Processor_t::logErr(const Instruction_t &instr, const string &s,
                         Error_t::Level_t level)
{
    error->logRuntimeError
        (level,
         Error_t::Position_t(((instr.sourceIndex < 0) ? "" :
                              program->getSource(instr.sourceIndex)),
                             instr.line,
                             instr.column),s);
}

void Processor_t::logErrNoInstr(const string &s,
                                Error_t::Level_t level)
{
    error->logRuntimeError(level,
                           Error_t::Position_t("unknown", 1, 0), s);
}

Processor_t::Processor_t(const Program_t *program,
                         const Dictionary_t *dict,
                         const Dictionary_t *param,
                         const string &encoding,
                         const ContentType_t &escaper,
                         bool errorFragment)
    : program(program), langDictionary(dict), paramDictionary(param),
      fParam(*this, encoding, escaper), errorFragment(errorFragment)
{
    srand(time(0) ^ getpid()); // because of user function random
    root.iteration = 0;
    root.name = "";
    root.number.setInteger(0);
    root.count.setInteger(1);
    oldFragmentStack.reserve(80);
}

int Processor_t::evalNumOp(const Instruction_t &instr) {
    if (valueStack.size() < 2) return -1;
    
    ParserValue_t b = valueStack.top();
    valueStack.pop();
    b.validateThis();
    
    ParserValue_t a = valueStack.top();
    valueStack.pop();
    a.validateThis();
    
    if ((a.type == ParserValue_t::TYPE_STRING) ||
        (b.type == ParserValue_t::TYPE_STRING)) {
        return -1; 
    }
    
    if ((a.type == ParserValue_t::TYPE_REAL) ||
        (b.type == ParserValue_t::TYPE_REAL)) {
        feclearexcept(FE_ALL_EXCEPT);
        
        switch (instr.operation) {
        case Instruction_t::BITAND:
        case Instruction_t::BITOR:
        case Instruction_t::BITXOR:
            return -1;
            
        case Instruction_t::ADD:
            a.setReal(a.realValue + b.realValue);
            break;
            
        case Instruction_t::SUB:
            a.setReal(a.realValue - b.realValue);
            break;
            
        case Instruction_t::MUL:
            a.setReal(a.realValue * b.realValue);
            break;
            
        case Instruction_t::DIV:
            a.setReal(a.realValue / b.realValue);
            break;
            
        case Instruction_t::MOD:
            if (!b.integerValue) return -1;
            a.setInteger(a.integerValue % b.integerValue);
            break;
            
        case Instruction_t::NUMEQ:
            a.setInteger(a.realValue == b.realValue);
            break;
            
        case Instruction_t::NUMGE:
            a.setInteger(a.realValue >= b.realValue);
            break; 
            
        case Instruction_t::NUMGT:
            a.setInteger(a.realValue > b.realValue);
            break;
            
        default:
            return -1;
        }
        
        if (fetestexcept(FE_ALL_EXCEPT) & (~FE_INEXACT)) {
            return -1;
        }
    } else {
        switch(instr.operation) { 
        case Instruction_t::BITAND:
            a.setInteger(a.integerValue & b.integerValue);
            break;
            
        case Instruction_t::BITOR:
            a.setInteger(a.integerValue | b.integerValue);
            break;
            
        case Instruction_t::BITXOR:
            a.setInteger(a.integerValue ^ b.integerValue);
            break;
            
        case Instruction_t::ADD:
            a.setInteger(a.integerValue + b.integerValue);
            break;
            
        case Instruction_t::SUB:
            a.setInteger(a.integerValue - b.integerValue);
            break;
            
        case Instruction_t::MUL:
            a.setInteger(a.integerValue * b.integerValue);
            break;
            
        case Instruction_t::DIV:
            if (!b.integerValue) return -1;
            a.setInteger(a.integerValue / b.integerValue);
            break;
            
        case Instruction_t::MOD:
            if (!b.integerValue) return -1;
            a.setInteger(a.integerValue % b.integerValue);
            break;
            
        case Instruction_t::NUMEQ:
            a.setInteger(a.integerValue == b.integerValue);
            break;
            
        case Instruction_t::NUMGE:
            a.setInteger(a.integerValue >= b.integerValue);
            break;
            
        case Instruction_t::NUMGT:
            a.setInteger(a.integerValue > b.integerValue);
            break;
            
        default:
            return -1;
        }
    }
    
    valueStack.push(a);
    return 0;
}

int Processor_t::numOp(const Instruction_t &instr) {
    
    if (valueStack.size() < 2) {
        logErr(instr, "Value stack underflow", Error_t::LL_FATAL);
        return -1;
    }
    
    ParserValue_t b = valueStack.top();
    valueStack.pop();
    b.validateThis();
    
    ParserValue_t a = valueStack.top();
    valueStack.pop();
    a.validateThis();
    
    if ((a.type == ParserValue_t::TYPE_STRING) ||
        (b.type == ParserValue_t::TYPE_STRING)) {
        logErr(instr, "Numeric operation on string", Error_t::LL_ERROR);
        a.setString("undefined");
    } else if ((a.type == ParserValue_t::TYPE_REAL) ||
               (b.type == ParserValue_t::TYPE_REAL)) {
        feclearexcept(FE_ALL_EXCEPT);
        
        switch (instr.operation) { 
        case Instruction_t::BITAND:
        case Instruction_t::BITOR:
        case Instruction_t::BITXOR:
            
            logErr(instr, "Bit operation with real",
                   Error_t::LL_ERROR);
            a.setString("undefined");
            break;
            
        case Instruction_t::ADD:
            a.setReal(a.realValue + b.realValue);
            break;
            
        case Instruction_t::SUB:
            a.setReal(a.realValue - b.realValue);
            break;
            
        case Instruction_t::MUL:
            a.setReal(a.realValue * b.realValue);
            break;
            
        case Instruction_t::DIV:
            a.setReal(a.realValue / b.realValue);
            break;
            
        case Instruction_t::MOD:
            if (!b.integerValue) {
                logErr(instr, "Modulo by zero", Error_t::LL_ERROR);
                a.setString("undefined");
                break;
            }
            a.setInteger(a.integerValue % b.integerValue);
            break;
            
        case Instruction_t::NUMEQ:
            a.setInteger(a.realValue == b.realValue);
            break;
            
        case Instruction_t::NUMGE:
            a.setInteger(a.realValue >= b.realValue);
            break; 
            
        case Instruction_t::NUMGT:
            a.setInteger(a.realValue > b.realValue);
            break;
            
        default:
            logErr(instr, "Internal error, unknown numeric operation",
                   Error_t::LL_FATAL);
            return -1;
        }
        
        if (fetestexcept(FE_ALL_EXCEPT) & (~FE_INEXACT) & (~FE_INVALID)) {
            logErr(instr, "Invalid floating point operation",
                   Error_t::LL_ERROR);
            a.setString("undefined");
        }
    } else {
        switch (instr.operation) {
        case Instruction_t::BITAND:
            a.setInteger(a.integerValue & b.integerValue);
            break;
            
        case Instruction_t::BITOR:
            a.setInteger(a.integerValue | b.integerValue);
            break;
            
        case Instruction_t::BITXOR:
            a.setInteger(a.integerValue ^ b.integerValue);
            break;
            
        case Instruction_t::ADD:
            a.setInteger(a.integerValue + b.integerValue);
            break;
            
        case Instruction_t::SUB:
            a.setInteger(a.integerValue - b.integerValue);
            break;
            
        case Instruction_t::MUL:
            a.setInteger(a.integerValue * b.integerValue);
            break;
            
        case Instruction_t::DIV:
            if (!b.integerValue) {
                logErr(instr, "Division by zero", Error_t::LL_ERROR);
                a.setString("undefined");
                break;
            }
            a.setInteger(a.integerValue / b.integerValue);
            break;
            
        case Instruction_t::MOD:
            if (!b.integerValue) {
                logErr(instr, "Modulo by zero", Error_t::LL_ERROR);
                a.setString("undefined");
                break;
            }
            a.setInteger(a.integerValue % b.integerValue);
            break;
            
        case Instruction_t::NUMEQ:
            a.setInteger(a.integerValue == b.integerValue);
            break;
            
        case Instruction_t::NUMGE:
            a.setInteger(a.integerValue >= b.integerValue);
            break;
            
        case Instruction_t::NUMGT:
            a.setInteger(a.integerValue > b.integerValue);
            break;
            
        default:
            logErr(instr, "Internal error, unknown numeric operation",
                   Error_t::LL_FATAL);
            return -1;
        }
    }
    
    valueStack.push(a);
    return 0;
}

int Processor_t::evalBinaryOp(const Instruction_t &instr) {
    
    if (valueStack.size() < 2) return -1;
    ParserValue_t b = valueStack.top();
    valueStack.pop();
    
    ParserValue_t a = valueStack.top();
    valueStack.pop();
    
    switch (instr.operation) {
    case Instruction_t::CONCAT:
        a.stringValue = a.stringValue + b.stringValue;
        break;
        
    case Instruction_t::STREQ:
        if (a.stringValue == b.stringValue) a.setInteger(1);
        else a.setInteger(0);
        break;
        
    default:
        return -1;
    }
    
    valueStack.push(a);
    return 0;
}

int Processor_t::binaryOp(const Instruction_t &instr) {
    if (valueStack.size() < 2) {
        logErr(instr, "Value stack underflow",
               Error_t::LL_FATAL);
        return -1;
    }
    
    ParserValue_t b = valueStack.top();
    valueStack.pop();
    
    ParserValue_t a = valueStack.top();
    valueStack.pop();
    
    switch (instr.operation) {
    case Instruction_t::CONCAT:
        a.stringValue = a.stringValue + b.stringValue;
        break;
        
    case Instruction_t::REPEAT:
        b.validateThis();
        if ((b.type != ParserValue_t::TYPE_INT) || (b.integerValue < 0)) {
            logErr(instr, "REPEAT with wrong second argument",
                   Error_t::LL_ERROR);
            a.setString("undefined");
            break;
        }
        
        {
            string s = a.stringValue;
            a.setString("");
            a.stringValue.reserve(s.size() * b.integerValue);
            for (long i = 0; i < b.integerValue; i++) {
                a.stringValue += s;
            }
        }
        break;
        
    case Instruction_t::STREQ:
        if (a.stringValue==b.stringValue)
            a.setInteger(1); else a.setInteger(0);
        break;
        
    default:
        logErr(instr, "Internal error, unknown operation",
               Error_t::LL_FATAL);
        
        return -1;
    }
    valueStack.push(a);
    return 0;
}

int Processor_t::dumpFragment(const Fragment_t *f,
                              const string &padding) {
    // dump all variables (no nestedFragments)
    for (Fragment_t::const_iterator i = f->begin(); i != f->end(); i++) {
        if (!i->second->nestedFragments) {
            if (output->write(padding)) return -1;
            if (output->write(i->first)) return -1;
            if (output->write(": \"")) return -1;
            if (i->second->value.size() > 40) {
                if (output->write(i->second->value.substr(0, 37) + "...\"\n"))
                    return -1;
            } else if (output->write(i->second->value + "\"\n")) return -1;
        }
    }
    
    // dump all fragments (nestedFragments non-null)
    for (Fragment_t::const_iterator i = f->begin(); i != f->end(); i++) {
        if (i->second->nestedFragments) {
            unsigned int k = 0;
            for (FragmentList_t::const_iterator
                     in = i->second->nestedFragments->begin();
                 in != i->second->nestedFragments->end(); ++in, ++k) {
                if (output->write(padding)) return -1;
                
                char s[20];
                if (output->write(i->first)) return -1;
                sprintf(s,"[%u]: \n", k);
                
                if (output->write(s)) return -1;
                if (dumpFragment(*in, padding + "    ")) return -1;
                if (output->write("\n")) return -1;
            }
        }
    }
    
    // OK
    return 0;
}

int Processor_t::instructionDebug() {
    output->write("Template sources:\n");
    const SourceList_t &pl = program->getSources();
    for (unsigned int i = 0; i != pl.size(); ++i) {
        if (output->write("    " + pl.getSource(i) + "\n"))
	    return -1;
    }
    
    output->write("\nLanguage dictionary sources:\n");
    const SourceList_t &l = langDictionary->getSources();
    for (unsigned int i = 0; i != pl.size(); ++i) {
        if (output->write("    " + l.getSource(i) + "\n"))
	    return -1;
    }
    
    if (output->write("\nConfiguration dictionary sources:\n")) return -1;
    const SourceList_t &p = paramDictionary->getSources();
    for (unsigned int i = 0; i != pl.size(); ++i) {
        if (output->write("    " + p.getSource(i) + "\n"))
	    return -1;
    }
    
    if (output->write("\nApplication data:\n")) return -1;
    return dumpFragment(data);
}


void Processor_t::run(const Fragment_t *_data, Formatter_t *_output,
                      Error_t *_error)
{
    ParserValue_t a;
    data = _data;
    output = _output;
    error = _error;
    
 //    oldFragmentStack.clear();
    vector<ParserValue_t> programStack;
    programStack.reserve(80);
    while (!valueStack.empty()) valueStack.pop();
    
    int ip = 0; // Never will be changed to unsigned !!
    //data->dump(cout);
    
    FragmentStack_t fragmentStack(data, *error, errorFragment);

    while (1) {
        if (ip < 0 || ip >= (int)program->size()) {
            logErrNoInstr("Instruction pointer went "
                          "out of program address space",
                          Error_t::LL_FATAL);
            goto flushReturn;
        }
        const Instruction_t &instr = (*program)[ip++];
        //       instr.dump(stdout);
        switch (instr.operation) {
        case Instruction_t::EXIST:
            a.setInteger(!fragmentStack.exists(instr.identifier));
            valueStack.push(a);
            break;
            
        case Instruction_t::DEBUG:
            instructionDebug();
            break;
            
        case Instruction_t::VAL:
            valueStack.push(instr.value);
            break;
            
        case Instruction_t::DICT:
            if (valueStack.empty()) {
                logErr(instr, "Value stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            a = valueStack.top();
            valueStack.pop();
            {
                const string *item;
                item = langDictionary->lookup(a.stringValue);
                if (item == 0)
                    item = paramDictionary->lookup(a.stringValue);
                if (item == 0) {
                    logErr(instr, "Dictionary item '" + a.stringValue +
                           "' was not found",
                           Error_t::LL_WARNING);
                    item = &a.stringValue;
                }
                a.setString(*item);
            }
            valueStack.push(a);
            break;
            
        case Instruction_t::VAR:
            if (fragmentStack.findVariable(instr.identifier, a)) {
                logErr(instr, "Variable '" + instr.value.stringValue
                       + "' is undefined",
                       Error_t::LL_WARNING);
                a = ParserValue_t();
            } else {
                // check whether we have to escape variable
                if (instr.value.integerValue)
                    a.setString(fParam.escaper.escape(a.stringValue));
            }
            valueStack.push(a);
            break;
            
        case Instruction_t::PUSH:
            if (valueStack.empty()) {
                logErr(instr, "Value stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            a = valueStack.top();
            valueStack.pop();
            if (programStack.size() >= programStack.capacity())
                programStack.reserve(programStack.size() + 80);
            programStack.push_back(a);
            break;
            
        case Instruction_t::POP:
            if (programStack.empty()) {
                logErr(instr, "Program stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            programStack.pop_back();
            break;
            
        case Instruction_t::STACK:
            if (instr.value.integerValue > 0 ||
                -instr.value.integerValue >= (int)programStack.size()) {
                logErr(instr, "Program stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            valueStack.push(programStack[programStack.size() - 1 + 
                                         instr.value.integerValue]);
            break;
            
        case Instruction_t::BITOR:   
        case Instruction_t::BITXOR:
        case Instruction_t::BITAND:
        case Instruction_t::ADD:
        case Instruction_t::SUB:
        case Instruction_t::MUL:
        case Instruction_t::DIV:
        case Instruction_t::MOD:
        case Instruction_t::NUMEQ:
        case Instruction_t::NUMGE:
        case Instruction_t::NUMGT:
            if (numOp(instr) < 0) goto flushReturn;
            break;
            
        case Instruction_t::CONCAT:
        case Instruction_t::STREQ:
        case Instruction_t::REPEAT:
            if (binaryOp(instr) < 0) goto flushReturn;
            break;
            
        case Instruction_t::NOT:
            if (valueStack.empty()) {
                logErr(instr, "Value stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            a = valueStack.top();
            valueStack.pop();
            a.setInteger(!a);
            valueStack.push(a);
            break;
            
        case Instruction_t::BITNOT:
            if (valueStack.empty()) {
                logErr(instr, "Value stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            a = valueStack.top();
            valueStack.pop();
            a.validateThis();
            if (a.type != ParserValue_t::TYPE_INT) {
                logErr(instr, "Bit operation not with integer",
                       Error_t::LL_ERROR);
                a.setString("undefined");
            } else a.setInteger(~a.integerValue);
            valueStack.push(a);
            break;
            
        case Instruction_t::FUNC:
            {
                long i = instr.value.integerValue;
                int j;
                if (i < 0) {
                    logErr(instr, "Negative function argument count",
                           Error_t::LL_FATAL);
                    goto flushReturn;
                }
                
                if ((int)valueStack.size() < i) {
                    logErr(instr, "Value stack underflow",
                           Error_t::LL_FATAL); 
                    goto flushReturn;
                }
                
                vector <ParserValue_t> v(i);
                for (j = 0; j < i; j++) {
                    v[j] = valueStack.top();
                    valueStack.pop();
                }
                
                Function_t p = tengFindFunction(instr.value.stringValue);
                if (p) {
                    fParam.logger.setInstruction(&instr);
                    switch (p(v, fParam, a)) {
                    case 0:
                        break; // OK
                    case -1:
                        logErr(instr, "Bad argument "
                               "count for function '"
                               + instr.value.stringValue + "()'",
                               Error_t::LL_ERROR);
                        break;
                    default:
                        logErr(instr, "Function '"
                               + instr.value.stringValue
                               + "()' call failed",
                               Error_t::LL_ERROR);
                    }
                    valueStack.push(a);
                } else {
                    logErr(instr, "Call to unknown function '"
                           + instr.value.stringValue + "()'",
                           Error_t::LL_ERROR);
                    a.setString("unknown");
                    valueStack.push(a);
                }
            }
            break;
            
        case Instruction_t::AND:
            if (valueStack.empty()) {
                logErr(instr, "Value stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            a = valueStack.top();
            if (!a) goto jump;
            valueStack.pop();
            break;
            
        case Instruction_t::OR:
            if (valueStack.empty()) {
                logErr(instr, "Value stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            a = valueStack.top();
            if (a) goto jump;
            valueStack.pop();
            break;
            
        case Instruction_t::JMPIFNOT:
            if (valueStack.empty()) {
                logErr(instr, "Value stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            a = valueStack.top();
            valueStack.pop();
            if (a) break;
            
        case Instruction_t::JMP:
        jump:
            ip += instr.value.integerValue;
            if (ip < 0 || ip >= (int)program->size()) {
                logErrNoInstr("Jump points out of program address space",
                              Error_t::LL_FATAL);
                goto flushReturn;
            }
            break;
            
        case Instruction_t::FORM:
            output->push((Formatter_t::Mode_t)instr.value.integerValue);
            break;
            
        case Instruction_t::ENDFORM:
            if (output->pop() < 0) {
                logErr(instr, "Format-object stack error",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            break;
            
        case Instruction_t::FRAG:
            if (fragmentStack.pushFrame(instr.identifier)) {
                // fragment has no iterations => ok, jump over fragment
                ip += instr.value.integerValue;
                if (ip < 0 || ip >= (int)program->size()) {
                    logErr(instr, "Fragment jump points out of "
                           "program address space",
                           Error_t::LL_FATAL);
                    goto flushReturn;
                }
            }
            break;
            
        case Instruction_t::ENDFRAG:
            if (fragmentStack.nextIteration()) {
                // next iteration
                ip += instr.value.integerValue;
                if (ip < 0 || ip >= (int)program->size()) {
                    logErr(instr, "End-fragment jump points out of "
                           "program address space",
                           Error_t::LL_FATAL);
                    goto flushReturn;
                }
            } else {
                // no more iterations, we have to pop frame
                if (fragmentStack.popFrame()) {
                    logErr(instr, "Fragment stack underflow",
                           Error_t::LL_FATAL);
                    goto flushReturn;
                }
            }
            break;
            
        case Instruction_t::FRAGCNT:
            {
                unsigned int fragmentSize = 0;
                if (fragmentStack.getFragmentSize(instr.identifier,
                                                  fragmentSize)) {
                    logErr(instr, "Fragment '" + instr.value.stringValue
                           + "' doesn't exist, cannot determine its size.",
                           Error_t::LL_WARNING);
                }
                a.setInteger(fragmentSize);
                valueStack.push(a);
            }
            break;

        case Instruction_t::XFRAGCNT:
            {
                // size of unopened fragment
                unsigned int fragmentSize = 0;
                if (fragmentStack.getSubFragmentSize(instr.identifier,
                                                     fragmentSize)) {
                    logErr(instr, "Fragment '" + instr.value.stringValue
                           + "' doesn't exist, cannot determine its size.",
                           Error_t::LL_WARNING);
                }
                a.setInteger(fragmentSize);
                valueStack.push(a);
            }
            break;

        case Instruction_t::FRAGITR:
            {
                unsigned int fragmentIteration = 0;
                if (fragmentStack.getFragmentIteration(instr.identifier,
                                                       fragmentIteration)) {
                    logErr(instr, "Fragment '" + instr.value.stringValue
                           + "' not open, cannot determine current iteration.",
                           Error_t::LL_WARNING);
                }
                a.setInteger(fragmentIteration);
                valueStack.push(a);
            }
            break;

        case Instruction_t::PRINT:
            if (valueStack.empty()) {
                logErr(instr, "Value stack underflow",
                       Error_t::LL_FATAL);
                goto flushReturn;
            }
            if (output->write(valueStack.top().stringValue)) {
                valueStack.pop();
                return;
            }
            valueStack.pop();
            break;
            
        case Instruction_t::SET:
            if (valueStack.empty()) {
                logErr(instr, "Value stack underflow",
                       Error_t::LL_FATAL); 
                goto flushReturn;
            }
            a = valueStack.top();
            valueStack.pop();

            switch (fragmentStack.setVariable(instr.identifier, a)) {
            case S_OK:
                // OK
                break;
            case S_ALREADY_DEFINED:
                logErr(instr,
                       "Cannot rewrite variable '" + instr.value.stringValue
                       + "' which is already set by the application.",
                       Error_t::LL_WARNING);
                break;
            default:
                logErr(instr,
                       "Cannot set variable '" + instr.value.stringValue
                       + "'.",
                       Error_t::LL_WARNING);
                break;
            }
            break;
            
        case Instruction_t::HALT:
            goto flushReturn;
            
        default:
            logErr(instr, "Unknown instruction",
                   Error_t::LL_FATAL);
            goto flushReturn;
        }
    }
 flushReturn:
    if (!oldFragmentStack.empty()) {
        logErrNoInstr("Fragment stack is not empty",
                      Error_t::LL_WARNING);
    }
    if (!programStack.empty()) {
        logErrNoInstr("Program stack is not empty",
                      Error_t::LL_WARNING);
    }
    if (!valueStack.empty()) {
        logErrNoInstr("Value stack is not empty",
                      Error_t::LL_WARNING);
    }
    output->flush();
}

int Processor_t::eval(ParserValue_t &result, int startAddress,
                      int endAddress)
{
    ParserValue_t a, b;
    int ip = startAddress; // Never will be changed to unsigned !!

    // we must have fake error
    Error_t fakeError;
    error = &fakeError;
    
    oldFragmentStack.clear();

    vector<ParserValue_t> programStack;
    programStack.reserve(80);

    while (!valueStack.empty()) valueStack.pop();
    if (endAddress > (int)program->size() || startAddress < 0) return -1;
    while (ip != endAddress) {
        if (ip < startAddress || ip > endAddress) {
            return -1;
        }
        const Instruction_t &instr = (*program)[ip++];
        //       instr.dump(stdout);
        switch (instr.operation) {
        case Instruction_t::REPEAT:
            if (b.type != ParserValue_t::TYPE_INT || b.integerValue < 0)
                return -1;
            {
                string s = a.stringValue;
                a.setString("");
                a.stringValue.reserve(s.size() * b.integerValue);
                for (long i = 0; i < b.integerValue; i++)
                    a.stringValue += s;
            }
            break;
            
        case Instruction_t::VAL:
            valueStack.push(instr.value);
            break;
            
        case Instruction_t::PUSH:
            if (valueStack.empty()) return -1;
            a = valueStack.top();
            valueStack.pop();
            if (programStack.size() >= programStack.capacity())
                programStack.reserve(programStack.size() + 80);
            programStack.push_back(a);
            break;
            
        case Instruction_t::POP:
            if (programStack.empty()) return -1;
            programStack.pop_back();
            break;
            
        case Instruction_t::STACK:
            if (instr.value.integerValue > 0 ||
                -instr.value.integerValue >= (int)programStack.size())
                return -1;
            valueStack.push(programStack[programStack.size() - 1 +
                                         instr.value.integerValue]);
            break;
            
        case Instruction_t::BITOR:   
        case Instruction_t::BITXOR:
        case Instruction_t::BITAND:
        case Instruction_t::ADD:
        case Instruction_t::SUB:
        case Instruction_t::MUL:
        case Instruction_t::DIV:
        case Instruction_t::MOD:
        case Instruction_t::NUMEQ:
        case Instruction_t::NUMGE:
        case Instruction_t::NUMGT:
            if (evalNumOp(instr) < 0) return -1;
            break;
            
        case Instruction_t::CONCAT:
        case Instruction_t::STREQ:
            if (evalBinaryOp(instr) < 0) return -1;
            break;
            
        case Instruction_t::BITNOT:
            if (valueStack.empty()) return -1;
            a = valueStack.top();
            valueStack.pop();
            a.validateThis();
            if (a.type != ParserValue_t::TYPE_INT) return -1;
            a.setInteger(~a.integerValue);
            valueStack.push(a);
            break;
            
        case Instruction_t::NOT:
            if (valueStack.empty()) return -1;
            a = valueStack.top();
            valueStack.pop();
            a.setInteger(!a);
            valueStack.push(a);
            break;
            
        case Instruction_t::FUNC:
            {
                long i = instr.value.integerValue;
                int j;
                if (i < 0) return -1;
                if ((int)valueStack.size() < i) return -1;
                vector <ParserValue_t> v(i);
                for (j = 0; j < i; j++) {
                    v[j] = valueStack.top();
                    valueStack.pop();
                }
                Function_t p = tengFindFunction(instr.value.stringValue, false);
                if (p) {
                    fParam.logger.setInstruction(&instr);
                    switch (p(v, fParam, a)) {
                    case 0:
                        break; // OK
                    case -1:
                        return -1;
                    default:
                        return -1;
                    }
                    valueStack.push(a);
                }
                else return -1;
            }
            break;
            
        case Instruction_t::AND:
            if (valueStack.empty()) return -1;
            a = valueStack.top();
            if (!a) goto jump;
            valueStack.pop();
            break;
            
        case Instruction_t::OR:
            if (valueStack.empty()) return -1;
            a = valueStack.top();
            if (a) goto jump;
            valueStack.pop();
            break;
            
        case Instruction_t::JMPIFNOT:
            if (valueStack.empty()) return -1;
            a=valueStack.top();
            valueStack.pop();
            if (a) break;
            
        case Instruction_t::JMP:
        jump:
            ip += instr.value.integerValue;
            break;
            
        default:
            return -1;
        }
    }
    if (valueStack.size() != 1) return -1;
    result = valueStack.top();
    return 0;
}