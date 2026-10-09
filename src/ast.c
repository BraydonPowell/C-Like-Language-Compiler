#include "ast.h"

const char *type_name(Type t) {
    switch (t) {
    case TY_VOID: return "void";
    case TY_INT: return "int";
    case TY_BOOL: return "bool";
    case TY_ERROR: return "<error>";
    }
    return "?";
}
