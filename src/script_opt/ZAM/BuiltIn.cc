// See the file "COPYING" in the main distribution directory for copyright.

// ZAM methods associated with instructions that replace calls to
// built-in functions.

#include "zeek/Func.h"
#include "zeek/Reporter.h"
#include "zeek/script_opt/ZAM/Compile.h"

namespace zeek::detail {

using GenBuiltIn = bool (ZAMCompiler::*)(const NameExpr* n, int nslot, const ExprPList& args);
struct BuiltInInfo {
    bool return_val_matters;
    GenBuiltIn func;
};

bool ZAMCompiler::IsZAM_BuiltIn(const Expr* e) {
    // The expression e is either directly a call (in which case there's
    // no return value), or an assignment to a call.
    const CallExpr* c;

    if ( e->Tag() == EXPR_CALL )
        c = e->AsCallExpr();
    else
        c = e->GetOp2()->AsCallExpr();

    auto func_expr = c->Func();
    if ( func_expr->Tag() != EXPR_NAME )
        // An indirect call.
        return false;

    auto func_val = func_expr->AsNameExpr()->Id()->GetVal();
    if ( ! func_val )
        // A call to a function that hasn't been defined.
        return false;

    auto func = func_val->AsFunc();
    if ( func->GetKind() != BuiltinFunc::BUILTIN_FUNC )
        return false;

    auto& args = c->Args()->Exprs();

    static std::map<std::string, BuiltInInfo> builtins = {
        {"Analyzer::__name", {true, &ZAMCompiler::BuiltIn_Analyzer__name}},
        {"Broker::__flush_logs", {false, &ZAMCompiler::BuiltIn_Broker__flush_logs}},
        {"Files::__enable_reassembly", {false, &ZAMCompiler::BuiltIn_Files__enable_reassembly}},
        {"Files::__set_reassembly_buffer", {false, &ZAMCompiler::BuiltIn_Files__set_reassembly_buffer}},
        {"Log::__write", {false, &ZAMCompiler::BuiltIn_Log__write}},
        {"cat", {true, &ZAMCompiler::BuiltIn_cat}},
        {"current_time", {true, &ZAMCompiler::BuiltIn_current_time}},
        {"get_port_transport_proto", {true, &ZAMCompiler::BuiltIn_get_port_etc}},
        {"network_time", {true, &ZAMCompiler::BuiltIn_network_time}},
        {"reading_live_traffic", {true, &ZAMCompiler::BuiltIn_reading_live_traffic}},
        {"reading_traces", {true, &ZAMCompiler::BuiltIn_reading_traces}},
        {"strstr", {true, &ZAMCompiler::BuiltIn_strstr}},
        {"sub_bytes", {true, &ZAMCompiler::BuiltIn_sub_bytes}},
        {"to_lower", {true, &ZAMCompiler::BuiltIn_to_lower}},
    };

    auto b = builtins.find(func->Name());
    if ( b == builtins.end() )
        return false;

    const auto& binfo = b->second;
    const NameExpr* n = nullptr; // name to assign to, if any
    if ( e->Tag() != EXPR_CALL )
        n = e->GetOp1()->AsRefExpr()->GetOp1()->AsNameExpr();

    if ( binfo.return_val_matters && ! n ) {
        reporter->Warning("return value from built-in function ignored");

        // The call is a no-op. We could return false here and have it
        // execute (for no purpose). We can also return true, which will
        // have the effect of just ignoring the statement.
        return true;
    }

    auto nslot = n ? Frame1Slot(n, OP1_WRITE) : -1;

    return (this->*(b->second.func))(n, nslot, args);
}

bool ZAMCompiler::BuiltIn_Analyzer__name(const NameExpr* n, int nslot, const ExprPList& args) {
    if ( args[0]->Tag() == EXPR_CONST )
        // Doesn't seem worth developing a variant for this weird
        // usage case.
        return false;

    auto arg_t = args[0]->AsNameExpr();

    auto z = ZInstI(OP_ANALYZER__NAME_VV, nslot, FrameSlot(arg_t));
    z.SetType(args[0]->GetType());

    AddInst(z);

    return true;
}

bool ZAMCompiler::BuiltIn_Broker__flush_logs(const NameExpr* n, int nslot, const ExprPList& args) {
    if ( n )
        AddInst(ZInstI(OP_BROKER_FLUSH_LOGS_V, Frame1Slot(n, OP1_WRITE)));
    else
        AddInst(ZInstI(OP_BROKER_FLUSH_LOGS_X));

    return true;
}

bool ZAMCompiler::BuiltIn_Files__enable_reassembly(const NameExpr* n, int nslot, const ExprPList& args) {
    if ( n )
        // While this built-in nominally returns a value, existing
        // script code ignores it, so for now we don't bother
        // special-casing the possibility that it doesn't.
        return false;

    if ( args[0]->Tag() == EXPR_CONST )
        // Weird!
        return false;

    auto arg_f = args[0]->AsNameExpr();

    AddInst(ZInstI(OP_FILES__ENABLE_REASSEMBLY_V, FrameSlot(arg_f)));

    return true;
}

bool ZAMCompiler::BuiltIn_Files__set_reassembly_buffer(const NameExpr* n, int nslot, const ExprPList& args) {
    if ( n )
        // See above for enable_reassembly
        return false;

    if ( args[0]->Tag() == EXPR_CONST )
        // Weird!
        return false;

    auto arg_f = FrameSlot(args[0]->AsNameExpr());

    ZInstI z;

    if ( args[1]->Tag() == EXPR_CONST ) {
        auto arg_cnt = args[1]->AsConstExpr()->Value()->AsCount();
        z = ZInstI(OP_FILES__SET_REASSEMBLY_BUFFER_VC, arg_f, arg_cnt);
        z.op_type = OP_VV_I2;
    }
    else
        z = ZInstI(OP_FILES__SET_REASSEMBLY_BUFFER_VV, arg_f, FrameSlot(args[1]->AsNameExpr()));

    AddInst(z);

    return true;
}

bool ZAMCompiler::BuiltIn_Log__write(const NameExpr* n, int nslot, const ExprPList& args) {
    auto id = args[0];
    auto columns = args[1];

    if ( columns->Tag() != EXPR_NAME )
        return false;

    auto columns_n = columns->AsNameExpr();
    auto col_slot = FrameSlot(columns_n);

    bool const_id = (id->Tag() == EXPR_CONST);

    ZInstAux* aux = nullptr;

    if ( const_id ) {
        aux = new ZInstAux(1);
        aux->Add(0, id->AsConstExpr()->ValuePtr());
    }

    ZInstI z;

    if ( n ) {
        if ( const_id ) {
            z = ZInstI(OP_LOG_WRITEC_VV, nslot, col_slot);
            z.aux = aux;
        }
        else
            z = ZInstI(OP_LOG_WRITE_VVV, nslot, FrameSlot(id->AsNameExpr()), col_slot);
    }
    else {
        if ( const_id ) {
            z = ZInstI(OP_LOG_WRITEC_V, col_slot, id->AsConstExpr());
            z.aux = aux;
        }
        else
            z = ZInstI(OP_LOG_WRITE_VV, FrameSlot(id->AsNameExpr()), col_slot);
    }

    z.SetType(columns_n->GetType());

    AddInst(z);

    return true;
}

bool ZAMCompiler::BuiltIn_cat(const NameExpr* n, int nslot, const ExprPList& args) {
    auto& a0 = args[0];
    ZInstI z;

    if ( args.empty() ) {
        // Weird, but easy enough to support.
        z = ZInstI(OP_CAT1_VC, nslot);
        z.t = n->GetType();
        z.c = ZVal(val_mgr->EmptyString());
    }

    else if ( args.size() > 1 ) {
        switch ( args.size() ) {
            case 2: z = GenInst(OP_CAT2_V, n); break;
            case 3: z = GenInst(OP_CAT3_V, n); break;
            case 4: z = GenInst(OP_CAT4_V, n); break;
            case 5: z = GenInst(OP_CAT5_V, n); break;
            case 6: z = GenInst(OP_CAT6_V, n); break;
            case 7: z = GenInst(OP_CAT7_V, n); break;
            case 8: z = GenInst(OP_CAT8_V, n); break;

            default: z = GenInst(OP_CATN_V, n); break;
        }

        z.aux = BuildCatAux(args);
    }

    else if ( a0->GetType()->Tag() != TYPE_STRING ) {
        if ( a0->Tag() == EXPR_NAME ) {
            z = GenInst(OP_CAT1FULL_VV, n, a0->AsNameExpr());
            z.t = a0->GetType();
        }
        else {
            z = ZInstI(OP_CAT1_VC, nslot);
            z.t = n->GetType();
            z.c = ZVal(ZAM_val_cat(a0->AsConstExpr()->ValuePtr()));
        }
    }

    else if ( a0->Tag() == EXPR_CONST ) {
        z = GenInst(OP_CAT1_VC, n, a0->AsConstExpr());
        z.t = n->GetType();
    }

    else
        z = GenInst(OP_CAT1_VV, n, a0->AsNameExpr());

    AddInst(z);

    return true;
}

ZInstAux* ZAMCompiler::BuildCatAux(const ExprPList& args) {
    auto n = args.size();
    auto aux = new ZInstAux(n);
    aux->cat_args = new std::unique_ptr<CatArg>[n];

    for ( size_t i = 0; i < n; ++i ) {
        auto& a_i = args[i];
        auto& t = a_i->GetType();

        std::unique_ptr<CatArg> ca;

        if ( a_i->Tag() == EXPR_CONST ) {
            auto c = a_i->AsConstExpr()->ValuePtr();
            aux->Add(i, c); // it will be ignored
            auto sv = ZAM_val_cat(c);
            auto s = sv->AsString();
            auto b = reinterpret_cast<char*>(s->Bytes());
            ca = std::make_unique<CatArg>(std::string(b, s->Len()));
        }

        else {
            auto slot = FrameSlot(a_i->AsNameExpr());
            aux->Add(i, slot, t);

            switch ( t->Tag() ) {
                case TYPE_BOOL:
                case TYPE_INT:
                case TYPE_COUNT:
                case TYPE_DOUBLE:
                case TYPE_TIME:
                case TYPE_ENUM:
                case TYPE_PORT:
                case TYPE_ADDR:
                case TYPE_SUBNET: ca = std::make_unique<FixedCatArg>(t); break;

                case TYPE_STRING: ca = std::make_unique<StringCatArg>(); break;

                case TYPE_PATTERN: ca = std::make_unique<PatternCatArg>(); break;

                default: ca = std::make_unique<DescCatArg>(t); break;
            }
        }

        aux->cat_args[i] = std::move(ca);
    }

    return aux;
}

bool ZAMCompiler::BuiltIn_current_time(const NameExpr* n, int nslot, const ExprPList& args) {
    AddInst(ZInstI(OP_CURRENT_TIME_V, nslot));
    return true;
}

bool ZAMCompiler::BuiltIn_get_port_etc(const NameExpr* n, int nslot, const ExprPList& args) {
    if ( args[0]->Tag() != EXPR_NAME )
        return false;

    auto pn = args[0]->AsNameExpr();
    AddInst(ZInstI(OP_GET_PORT_TRANSPORT_PROTO_VV, nslot, FrameSlot(pn)));
    return true;
}

bool ZAMCompiler::BuiltIn_network_time(const NameExpr* n, int nslot, const ExprPList& args) {
    AddInst(ZInstI(OP_NETWORK_TIME_V, nslot));
    return true;
}

bool ZAMCompiler::BuiltIn_reading_live_traffic(const NameExpr* n, int nslot, const ExprPList& args) {
    AddInst(ZInstI(OP_READING_LIVE_TRAFFIC_V, nslot));
    return true;
}

bool ZAMCompiler::BuiltIn_reading_traces(const NameExpr* n, int nslot, const ExprPList& args) {
    AddInst(ZInstI(OP_READING_TRACES_V, nslot));
    return true;
}

bool ZAMCompiler::BuiltIn_strstr(const NameExpr* n, int nslot, const ExprPList& args) {
    auto big = args[0];
    auto little = args[1];

    auto big_n = big->Tag() == EXPR_NAME ? big->AsNameExpr() : nullptr;
    auto little_n = little->Tag() == EXPR_NAME ? little->AsNameExpr() : nullptr;

    ZInstI z;

    if ( big_n && little_n )
        z = GenInst(OP_STRSTR_VVV, n, big_n, little_n);
    else if ( big_n )
        z = GenInst(OP_STRSTR_VVC, n, big_n, little->AsConstExpr());
    else if ( little_n )
        z = GenInst(OP_STRSTR_VCV, n, little_n, big->AsConstExpr());
    else
        return false;

    AddInst(z);

    return true;
}

bool ZAMCompiler::BuiltIn_sub_bytes(const NameExpr* n, int nslot, const ExprPList& args) {
    auto arg_s = args[0];
    auto arg_start = args[1];
    auto arg_n = args[2];

    int v2 = FrameSlotIfName(arg_s);
    int v3 = ConvertToCount(arg_start);
    int v4 = ConvertToInt(arg_n);

    auto c = arg_s->Tag() == EXPR_CONST ? arg_s->AsConstExpr() : nullptr;

    ZInstI z;

    switch ( ConstArgsMask(args, 3) ) {
        case 0x0: // all variable
            z = ZInstI(OP_SUB_BYTES_VVVV, nslot, v2, v3, v4);
            z.op_type = OP_VVVV;
            break;

        case 0x1: // last argument a constant
            z = ZInstI(OP_SUB_BYTES_VVVi, nslot, v2, v3, v4);
            z.op_type = OP_VVVV_I4;
            break;

        case 0x2: // 2nd argument a constant; flip!
            z = ZInstI(OP_SUB_BYTES_VViV, nslot, v2, v4, v3);
            z.op_type = OP_VVVV_I4;
            break;

        case 0x3: // both 2nd and third are constants
            z = ZInstI(OP_SUB_BYTES_VVii, nslot, v2, v3, v4);
            z.op_type = OP_VVVV_I3_I4;
            break;

        case 0x4: // first argument a constant
            ASSERT(c);
            z = ZInstI(OP_SUB_BYTES_VVVC, nslot, v3, v4, c);
            z.op_type = OP_VVVC;
            break;

        case 0x5: // first and third constant
            ASSERT(c);
            z = ZInstI(OP_SUB_BYTES_VViC, nslot, v3, v4, c);
            z.op_type = OP_VVVC_I3;
            break;

        case 0x6: // first and second constant - flip!
            ASSERT(c);
            z = ZInstI(OP_SUB_BYTES_ViVC, nslot, v4, v3, c);
            z.op_type = OP_VVVC_I3;
            break;

        case 0x7: // whole shebang
            ASSERT(c);
            z = ZInstI(OP_SUB_BYTES_ViiC, nslot, v3, v4, c);
            z.op_type = OP_VVVC_I2_I3;
            break;

        default: reporter->InternalError("bad constant mask");
    }

    AddInst(z);

    return true;
}

bool ZAMCompiler::BuiltIn_to_lower(const NameExpr* n, int nslot, const ExprPList& args) {
    if ( args[0]->Tag() == EXPR_CONST ) {
        auto arg_c = args[0]->AsConstExpr()->Value()->AsStringVal();
        ValPtr arg_lc = {AdoptRef{}, ZAM_to_lower(arg_c)};
        auto arg_lce = make_intrusive<ConstExpr>(arg_lc);
        auto z = ZInstI(OP_ASSIGN_CONST_VC, nslot, arg_lce.get());
        z.is_managed = true;
        AddInst(z);
    }

    else {
        auto arg_s = args[0]->AsNameExpr();
        AddInst(ZInstI(OP_TO_LOWER_VV, nslot, FrameSlot(arg_s)));
    }

    return true;
}

zeek_uint_t ZAMCompiler::ConstArgsMask(const ExprPList& args, int nargs) const {
    ASSERT(args.length() == nargs);

    zeek_uint_t mask = 0;

    for ( int i = 0; i < nargs; ++i ) {
        mask <<= 1;
        if ( args[i]->Tag() == EXPR_CONST )
            mask |= 1;
    }

    return mask;
}

} // namespace zeek::detail
