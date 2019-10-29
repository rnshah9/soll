// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "soll/Parse/Parser.h"
#include "soll/AST/AST.h"
#include "soll/Basic/DiagnosticParse.h"
#include "soll/Basic/OperatorPrecedence.h"
#include "soll/Lex/Lexer.h"

using namespace std;

namespace {

std::string stringUnquote(const std::string &Quoted) {
  std::string Result;
  assert(Quoted.size() >= 2 && "string token with size < 2!");
  const char *TokBegin = Quoted.data();
  const char *TokEnd = TokBegin + Quoted.size() - 1;
  assert(TokBegin[0] == '"' || TokBegin[0] == '\'');
  assert(TokEnd[0] == TokBegin[0]);
  ++TokBegin;

  const char *TokBuf = TokBegin;
  while (TokBuf != TokEnd) {
    if (TokBuf[0] != '\\') {
      const char *InStart = TokBuf;
      do {
        ++TokBuf;
      } while (TokBuf != TokEnd && TokBuf[0] != '\\');
      Result.append(InStart, TokBuf - InStart);
      continue;
    }
    if (TokBuf[1] == 'u') {
      std::uint_fast8_t UcnLen = 4;
      std::uint32_t UcnVal = 0;
      TokBuf += 2;
      for (; TokBuf != TokEnd && UcnLen; --UcnLen) {
        unsigned CharVal = llvm::hexDigitValue(TokBuf[0]);
        UcnVal <<= 4;
        UcnVal |= CharVal;
      }
      assert(UcnLen == 0 && "Unicode escape incompleted");

      // Convert to UTF8
      std::uint_fast8_t BytesToWrite = 0;
      if (UcnVal < 0x80U) {
        BytesToWrite = 1;
      } else if (UcnVal < 0x800U) {
        BytesToWrite = 2;
      } else if (UcnVal < 0x10000U) {
        BytesToWrite = 3;
      } else {
        BytesToWrite = 4;
      }
      constexpr const unsigned ByteMask = 0xBF;
      constexpr const unsigned ByteMark = 0x80;
      static constexpr const unsigned FirstByteMark[5] = {0x00, 0x00, 0xC0,
                                                          0xE0, 0xF0};
      std::array<char, 4> Buffer;
      char *ResultBuf = &Buffer[5];
      switch (BytesToWrite) {
      case 4:
        *--ResultBuf = static_cast<char>((UcnVal | ByteMark) & ByteMask);
        UcnVal >>= 6;
        [[fallthrough]];
      case 3:
        *--ResultBuf = static_cast<char>((UcnVal | ByteMark) & ByteMask);
        UcnVal >>= 6;
        [[fallthrough]];
      case 2:
        *--ResultBuf = static_cast<char>((UcnVal | ByteMark) & ByteMask);
        UcnVal >>= 6;
        [[fallthrough]];
      case 1:
        *--ResultBuf = static_cast<char>(UcnVal | FirstByteMark[BytesToWrite]);
      }
      Result.append(ResultBuf, BytesToWrite);
      continue;
    }
    TokBuf += 2;
    char ResultChar = TokBuf[1];
    switch (ResultChar) {
    case 'x': {
      std::uint_fast8_t HexLen = 2;
      std::uint8_t HexVal = 0;
      for (; TokBuf != TokEnd && HexLen; --HexLen) {
        unsigned CharVal = llvm::hexDigitValue(TokBuf[0]);
        HexVal <<= 4;
        HexVal |= CharVal;
      }
      assert(HexLen == 0 && "Hex escape incompleted");
      ResultChar = static_cast<char>(HexVal);
      break;
    }
    case '\\':
    case '\'':
    case '\"':
      break;
    case 'b':
      ResultChar = '\b';
      break;
    case 'f':
      ResultChar = '\f';
      break;
    case 'n':
      ResultChar = '\n';
      break;
    case 'r':
      ResultChar = '\r';
      break;
    case 't':
      ResultChar = '\t';
      break;
    case 'v':
      ResultChar = '\v';
      break;
    default:
      assert(false && "unknown escape sequence!");
      break;
    }
    Result.push_back(ResultChar);
  }
  return Result;
}

std::string hexUnquote(const std::string &Quoted) {
  assert(Quoted.size() % 2 == 0 && "Hex escape incompleted");
  std::string Result;
  for (std::size_t I = 0; I < Quoted.size(); I += 2) {
    Result.push_back((llvm::hexDigitValue(Quoted[I]) << 4) |
                     llvm::hexDigitValue(Quoted[I + 1]));
  }
  return Result;
}

} // namespace

namespace soll {

static BinaryOperatorKind token2bop(const Token &Tok) {
  switch (Tok.getKind()) {
  case tok::starstar:
    return BO_Exp;
  case tok::star:
    return BO_Mul;
  case tok::slash:
    return BO_Div;
  case tok::percent:
    return BO_Rem;
  case tok::plus:
    return BO_Add;
  case tok::minus:
    return BO_Sub;
  case tok::lessless:
    return BO_Shl;
  case tok::greatergreater:
    return BO_Shr;
  case tok::amp:
    return BO_And;
  case tok::caret:
    return BO_Xor;
  case tok::pipe:
    return BO_Or;
  case tok::less:
    return BO_LT;
  case tok::greater:
    return BO_GT;
  case tok::lessequal:
    return BO_LE;
  case tok::greaterequal:
    return BO_GE;
  case tok::equalequal:
    return BO_EQ;
  case tok::exclaimequal:
    return BO_NE;
  case tok::ampamp:
    return BO_LAnd;
  case tok::pipepipe:
    return BO_LOr;
  case tok::equal:
    return BO_Assign;
  case tok::starequal:
    return BO_MulAssign;
  case tok::slashequal:
    return BO_DivAssign;
  case tok::percentequal:
    return BO_RemAssign;
  case tok::plusequal:
    return BO_AddAssign;
  case tok::minusequal:
    return BO_SubAssign;
  case tok::lesslessequal:
    return BO_ShlAssign;
  case tok::greatergreaterequal:
    return BO_ShrAssign;
  case tok::ampequal:
    return BO_AndAssign;
  case tok::caretequal:
    return BO_XorAssign;
  case tok::pipeequal:
    return BO_OrAssign;
  case tok::comma:
    return BO_Comma;
  default:
    return BO_Undefined;
  }
}

static UnaryOperatorKind token2uop(const Token &Tok, bool IsPreOp = true) {
  switch (Tok.getKind()) {
  case tok::plusplus:
    if (IsPreOp)
      return UO_PreInc;
    else
      return UO_PostInc;
  case tok::minusminus:
    if (IsPreOp)
      return UO_PreDec;
    else
      return UO_PostDec;
  case tok::amp:
    return UO_AddrOf;
  case tok::star:
    return UO_Deref;
  case tok::plus:
    return UO_Plus;
  case tok::minus:
    return UO_Minus;
  case tok::tilde:
    return UO_Not;
  case tok::exclaim:
    return UO_LNot;
  default:
    return UO_Undefined;
  }
}

static IntegerType::IntKind token2inttype(const Token &Tok) {
  switch (Tok.getKind()) {
  case tok::kw_uint8:
    return IntegerType::IntKind::U8;
  case tok::kw_uint16:
    return IntegerType::IntKind::U16;
  case tok::kw_uint24:
    return IntegerType::IntKind::U24;
  case tok::kw_uint32:
    return IntegerType::IntKind::U32;
  case tok::kw_uint40:
    return IntegerType::IntKind::U40;
  case tok::kw_uint48:
    return IntegerType::IntKind::U48;
  case tok::kw_uint56:
    return IntegerType::IntKind::U56;
  case tok::kw_uint64:
    return IntegerType::IntKind::U64;
  case tok::kw_uint72:
    return IntegerType::IntKind::U72;
  case tok::kw_uint80:
    return IntegerType::IntKind::U80;
  case tok::kw_uint88:
    return IntegerType::IntKind::U88;
  case tok::kw_uint96:
    return IntegerType::IntKind::U96;
  case tok::kw_uint104:
    return IntegerType::IntKind::U104;
  case tok::kw_uint112:
    return IntegerType::IntKind::U112;
  case tok::kw_uint120:
    return IntegerType::IntKind::U120;
  case tok::kw_uint128:
    return IntegerType::IntKind::U128;
  case tok::kw_uint136:
    return IntegerType::IntKind::U136;
  case tok::kw_uint144:
    return IntegerType::IntKind::U144;
  case tok::kw_uint152:
    return IntegerType::IntKind::U152;
  case tok::kw_uint160:
    return IntegerType::IntKind::U160;
  case tok::kw_uint168:
    return IntegerType::IntKind::U168;
  case tok::kw_uint176:
    return IntegerType::IntKind::U176;
  case tok::kw_uint184:
    return IntegerType::IntKind::U184;
  case tok::kw_uint192:
    return IntegerType::IntKind::U192;
  case tok::kw_uint200:
    return IntegerType::IntKind::U200;
  case tok::kw_uint208:
    return IntegerType::IntKind::U208;
  case tok::kw_uint216:
    return IntegerType::IntKind::U216;
  case tok::kw_uint224:
    return IntegerType::IntKind::U224;
  case tok::kw_uint232:
    return IntegerType::IntKind::U232;
  case tok::kw_uint240:
    return IntegerType::IntKind::U240;
  case tok::kw_uint248:
    return IntegerType::IntKind::U248;
  case tok::kw_uint256:
    return IntegerType::IntKind::U256;
  case tok::kw_uint:
    return IntegerType::IntKind::U256;
  case tok::kw_int8:
    return IntegerType::IntKind::I8;
  case tok::kw_int16:
    return IntegerType::IntKind::I16;
  case tok::kw_int24:
    return IntegerType::IntKind::I24;
  case tok::kw_int32:
    return IntegerType::IntKind::I32;
  case tok::kw_int40:
    return IntegerType::IntKind::I40;
  case tok::kw_int48:
    return IntegerType::IntKind::I48;
  case tok::kw_int56:
    return IntegerType::IntKind::I56;
  case tok::kw_int64:
    return IntegerType::IntKind::I64;
  case tok::kw_int72:
    return IntegerType::IntKind::I72;
  case tok::kw_int80:
    return IntegerType::IntKind::I80;
  case tok::kw_int88:
    return IntegerType::IntKind::I88;
  case tok::kw_int96:
    return IntegerType::IntKind::I96;
  case tok::kw_int104:
    return IntegerType::IntKind::I104;
  case tok::kw_int112:
    return IntegerType::IntKind::I112;
  case tok::kw_int120:
    return IntegerType::IntKind::I120;
  case tok::kw_int128:
    return IntegerType::IntKind::I128;
  case tok::kw_int136:
    return IntegerType::IntKind::I136;
  case tok::kw_int144:
    return IntegerType::IntKind::I144;
  case tok::kw_int152:
    return IntegerType::IntKind::I152;
  case tok::kw_int160:
    return IntegerType::IntKind::I160;
  case tok::kw_int168:
    return IntegerType::IntKind::I168;
  case tok::kw_int176:
    return IntegerType::IntKind::I176;
  case tok::kw_int184:
    return IntegerType::IntKind::I184;
  case tok::kw_int192:
    return IntegerType::IntKind::I192;
  case tok::kw_int200:
    return IntegerType::IntKind::I200;
  case tok::kw_int208:
    return IntegerType::IntKind::I208;
  case tok::kw_int216:
    return IntegerType::IntKind::I216;
  case tok::kw_int224:
    return IntegerType::IntKind::I224;
  case tok::kw_int232:
    return IntegerType::IntKind::I232;
  case tok::kw_int240:
    return IntegerType::IntKind::I240;
  case tok::kw_int248:
    return IntegerType::IntKind::I248;
  case tok::kw_int256:
    return IntegerType::IntKind::I256;
  case tok::kw_int:
    return IntegerType::IntKind::I256;
  default:
    assert(false && "Invalid int token.");
  }
  LLVM_BUILTIN_UNREACHABLE;
}

static FixedBytesType::ByteKind token2bytetype(const Token &Tok) {
  switch (Tok.getKind()) {
  case tok::kw_bytes1:
    return FixedBytesType::ByteKind::B1;
  case tok::kw_bytes2:
    return FixedBytesType::ByteKind::B2;
  case tok::kw_bytes3:
    return FixedBytesType::ByteKind::B3;
  case tok::kw_bytes4:
    return FixedBytesType::ByteKind::B4;
  case tok::kw_bytes5:
    return FixedBytesType::ByteKind::B5;
  case tok::kw_bytes6:
    return FixedBytesType::ByteKind::B6;
  case tok::kw_bytes7:
    return FixedBytesType::ByteKind::B7;
  case tok::kw_bytes8:
    return FixedBytesType::ByteKind::B8;
  case tok::kw_bytes9:
    return FixedBytesType::ByteKind::B9;
  case tok::kw_bytes10:
    return FixedBytesType::ByteKind::B10;
  case tok::kw_bytes11:
    return FixedBytesType::ByteKind::B11;
  case tok::kw_bytes12:
    return FixedBytesType::ByteKind::B12;
  case tok::kw_bytes13:
    return FixedBytesType::ByteKind::B13;
  case tok::kw_bytes14:
    return FixedBytesType::ByteKind::B14;
  case tok::kw_bytes15:
    return FixedBytesType::ByteKind::B15;
  case tok::kw_bytes16:
    return FixedBytesType::ByteKind::B16;
  case tok::kw_bytes17:
    return FixedBytesType::ByteKind::B17;
  case tok::kw_bytes18:
    return FixedBytesType::ByteKind::B18;
  case tok::kw_bytes19:
    return FixedBytesType::ByteKind::B19;
  case tok::kw_bytes20:
    return FixedBytesType::ByteKind::B20;
  case tok::kw_bytes21:
    return FixedBytesType::ByteKind::B21;
  case tok::kw_bytes22:
    return FixedBytesType::ByteKind::B22;
  case tok::kw_bytes23:
    return FixedBytesType::ByteKind::B23;
  case tok::kw_bytes24:
    return FixedBytesType::ByteKind::B24;
  case tok::kw_bytes25:
    return FixedBytesType::ByteKind::B25;
  case tok::kw_bytes26:
    return FixedBytesType::ByteKind::B26;
  case tok::kw_bytes27:
    return FixedBytesType::ByteKind::B27;
  case tok::kw_bytes28:
    return FixedBytesType::ByteKind::B28;
  case tok::kw_bytes29:
    return FixedBytesType::ByteKind::B29;
  case tok::kw_bytes30:
    return FixedBytesType::ByteKind::B30;
  case tok::kw_bytes31:
    return FixedBytesType::ByteKind::B31;
  case tok::kw_bytes32:
    return FixedBytesType::ByteKind::B32;
  default:
    assert(false && "Invalid int token.");
  }
  LLVM_BUILTIN_UNREACHABLE;
}

Parser::Parser(Lexer &TheLexer, Sema &Actions, DiagnosticsEngine &Diags)
    : TheLexer(TheLexer), Actions(Actions), Diags(Diags) {
  Tok = *TheLexer.CachedLex();
}

unique_ptr<SourceUnit> Parser::parse() {
  ParseScope SourceUnitScope{this, 0};
  llvm::Optional<Token> CurTok;
  vector<unique_ptr<Decl>> Nodes;

  while (Tok.isNot(tok::eof)) {
    switch (Tok.getKind()) {
    case tok::kw_pragma:
      Nodes.push_back(parsePragmaDirective());
      break;
    case tok::kw_import:
      ConsumeToken();
      break;
    case tok::kw_interface:
    case tok::kw_library:
    case tok::kw_contract: {
      Nodes.push_back(parseContractDefinition());
      break;
    }
    default:
      ConsumeAnyToken();
      break;
    }
  }
  return make_unique<SourceUnit>(std::move(Nodes));
}

unique_ptr<PragmaDirective> Parser::parsePragmaDirective() {
  // pragma anything* ;
  // Currently supported:
  // pragma solidity ^0.4.0 || ^0.3.0;
  vector<string> Literals;
  vector<Token> Tokens;
  ConsumeToken(); // 'pragma'
  do {
    const tok::TokenKind Kind = Tok.getKind();
    switch (Kind) {
#define PUNCTUATOR(X, Y) case tok::X:
#include "soll/Basic/TokenKinds.def"
      Tokens.push_back(Tok);
      Literals.push_back(tok::getPunctuatorSpelling(Kind));
      ConsumeToken();
      break;
    case tok::raw_identifier:
    case tok::identifier:
      Tokens.push_back(Tok);
      Literals.emplace_back(Tok.getIdentifierInfo()->getName().str());
      ConsumeToken();
      break;
    case tok::numeric_constant:
      Tokens.push_back(Tok);
      Literals.emplace_back(Tok.getLiteralData(), Tok.getLength());
      ConsumeToken();
      break;
    default:
      Diag(diag::err_unknown_pragma);
      ConsumeAnyToken();
      break;
    }
  } while (!Tok.isOneOf(tok::semi, tok::eof));
  if (ExpectAndConsumeSemi()) {
    return nullptr;
  }

  // TODO: Implement version recognize and compare. ref: parsePragmaVersion
  return std::make_unique<PragmaDirective>();
}

ContractDecl::ContractKind Parser::parseContractKind() {
  switch (Tok.getKind()) {
  case tok::kw_interface:
    ConsumeToken();
    return ContractDecl::ContractKind::Interface;
  case tok::kw_contract:
    ConsumeToken();
    return ContractDecl::ContractKind::Contract;
  case tok::kw_library:
    ConsumeToken();
    return ContractDecl::ContractKind::Library;
  default:
    Diag(diag::err_expected_contract_kind);
    ConsumeAnyToken();
    return ContractDecl::ContractKind::Contract;
  }
}

Decl::Visibility Parser::parseVisibilitySpecifier() {
  switch (Tok.getKind()) {
  case tok::kw_public:
    ConsumeToken();
    return Decl::Visibility::Public;
  case tok::kw_internal:
    ConsumeToken();
    return Decl::Visibility::Internal;
  case tok::kw_private:
    ConsumeToken();
    return Decl::Visibility::Private;
  case tok::kw_external:
    ConsumeToken();
    return Decl::Visibility::External;
  default:
    Diag(diag::err_expected_visibility);
    ConsumeAnyToken();
    return Decl::Visibility::Default;
  }
}

StateMutability Parser::parseStateMutability() {
  StateMutability stateMutability(StateMutability::NonPayable);
  switch (Tok.getKind()) {
  case tok::kw_payable:
    ConsumeToken();
    return StateMutability::Payable;
  case tok::kw_view:
    ConsumeToken();
    return StateMutability::View;
  case tok::kw_pure:
    ConsumeToken();
    return StateMutability::Pure;
  case tok::kw_constant:
    Diag(diag::warn_constant_removed);
    ConsumeToken();
    return StateMutability::View;
  default:
    Diag(diag::err_expected_state_mutability);
    ConsumeAnyToken();
    return StateMutability::NonPayable;
    assert(false && "Invalid state mutability specifier.");
  }
  return stateMutability;
}

DataLocation Parser::parseDataLocation() {
  switch (Tok.getKind()) {
  case tok::kw_storage:
    ConsumeToken();
    return DataLocation::Storage;
  case tok::kw_memory:
    ConsumeToken();
    return DataLocation::Memory;
  case tok::kw_calldata:
    ConsumeToken();
    return DataLocation::CallData;
  default:
    return DataLocation::Storage;
  }
}

unique_ptr<ContractDecl> Parser::parseContractDefinition() {
  ParseScope ContractScope{this, 0};
  ContractDecl::ContractKind CtKind = parseContractKind();
  if (!Tok.isAnyIdentifier()) {
    Diag(diag::err_expected) << tok::identifier;
    return nullptr;
  }
  llvm::StringRef Name = Tok.getIdentifierInfo()->getName();
  ConsumeToken();

  vector<unique_ptr<InheritanceSpecifier>> BaseContracts;
  vector<unique_ptr<Decl>> SubNodes;
  unique_ptr<FunctionDecl> Constructor;
  unique_ptr<FunctionDecl> Fallback;

  if (TryConsumeToken(tok::kw_is)) {
    do {
      // TODO: Update vector<InheritanceSpecifier> baseContracts
      if (!Tok.isAnyIdentifier()) {
        Diag(diag::err_expected) << tok::identifier;
        return nullptr;
      }
      string BaseName = Tok.getIdentifierInfo()->getName();
      ConsumeToken(); // identifier

      vector<std::unique_ptr<Expr>> Arguments;
      if (isTokenParen()) {
        ConsumeParen();
        while (!isTokenParen()) {
          if (ExpectAndConsume(tok::comma)) {
            return nullptr;
          }
          Arguments.emplace_back(parseExpression());
        }
      }
      BaseContracts.emplace_back(std::make_unique<InheritanceSpecifier>(
          std::move(BaseName), std::move(Arguments)));
    } while (TryConsumeToken(tok::comma));
  }

  if (ExpectAndConsume(tok::l_brace)) {
    return nullptr;
  }

  while (Tok.isNot(tok::eof)) {
    if (Tok.is(tok::r_brace)) {
      ConsumeBrace();
      break;
    }

    // TODO: < Parse all Types in contract's context >
    if (Tok.isOneOf(tok::kw_function, tok::kw_constructor)) {
      auto FD = parseFunctionDefinitionOrFunctionTypeStateVariable();
      if (FD) {
        Actions.addDecl(FD.get());
        if (FD->isConstructor()) {
          assert(!Constructor && "multiple constructor defined!");
          Constructor = std::move(FD);
        } else if (FD->isFallback()) {
          assert(!Fallback && "multiple fallback defined!");
          Fallback = std::move(FD);
        } else {
          SubNodes.push_back(std::move(FD));
        }
      }
      Actions.EraseFunRtnTys();
    } else if (Tok.is(tok::kw_struct)) {
      // TODO: contract tok::kw_struct
      Diag(diag::err_unimplemented_token) << tok::kw_struct;
      return nullptr;
    } else if (Tok.is(tok::kw_enum)) {
      // TODO: contract tok::kw_enum
      Diag(diag::err_unimplemented_token) << tok::kw_enum;
      return nullptr;
    } else if (Tok.isElementaryTypeName() || Tok.isAnyIdentifier() ||
               Tok.is(tok::kw_mapping)) {
      VarDeclParserOptions options;
      options.IsStateVariable = true;
      options.AllowInitialValue = true;
      SubNodes.push_back(parseVariableDeclaration(options));
      if (ExpectAndConsumeSemi()) {
        return nullptr;
      }
    } else if (Tok.is(tok::kw_modifier)) {
      // TODO: contract tok::kw_modifier
      Diag(diag::err_unimplemented_token) << tok::kw_modifier;
      return nullptr;
    } else if (TryConsumeToken(tok::kw_event)) {
      SubNodes.push_back(parseEventDefinition());
    } else if (Tok.is(tok::kw_using)) {
      // TODO: contract tok::kw_using
      Diag(diag::err_unimplemented_token) << tok::kw_using;
      return nullptr;
    } else {
      Diag(diag::err_expected_contract_part);
      return nullptr;
    }
  }
  auto CD = std::make_unique<ContractDecl>(
      Name, std::move(BaseContracts), std::move(SubNodes),
      std::move(Constructor), std::move(Fallback), CtKind);
  Actions.addDecl(CD.get());
  for (auto &LPD : LateParsedDeclarations) {
    LPD->ParseLexedMethodDefs();
  }
  LateParsedDeclarations.clear();
  return CD;
}

Parser::FunctionHeaderParserResult
Parser::parseFunctionHeader(bool ForceEmptyName, bool AllowModifiers) {

  FunctionHeaderParserResult Result;

  Result.IsConstructor = false;
  Result.IsFallback = false;

  if (Tok.is(tok::kw_constructor)) {
    Result.IsConstructor = true;
  } else {
    assert(Tok.is(tok::kw_function));
  }
  ConsumeToken();

  if (Result.IsConstructor) {
    Result.Name = llvm::StringRef("solidity.constructor");
  } else if (ForceEmptyName || Tok.is(tok::l_paren)) {
    Result.Name = llvm::StringRef("solidity.fallback");
    Result.IsFallback = true;
  } else if (Tok.isAnyIdentifier()) {
    Result.Name = Tok.getIdentifierInfo()->getName();
    ConsumeToken(); // identifier
  } else {
    assert(false);
  }

  VarDeclParserOptions Options;
  Options.AllowLocationSpecifier = true;

  Result.Parameters = parseParameterList(Options);

  while (true) {
    if (AllowModifiers && Tok.is(tok::identifier)) {
      // TODO: Function Modifier
    } else if (Tok.isOneOf(tok::kw_public, tok::kw_private, tok::kw_internal,
                           tok::kw_external)) {
      // TODO: Special case of a public state variable of function Type.
      Result.Vsblty = parseVisibilitySpecifier();
    } else if (Tok.isOneOf(tok::kw_constant, tok::kw_pure, tok::kw_view,
                           tok::kw_payable)) {
      Result.SM = parseStateMutability();
    } else {
      break;
    }
  }

  if (TryConsumeToken(tok::kw_returns)) {
    bool const PermitEmptyParameterList = false;
    Result.ReturnParameters =
        parseParameterList(Options, PermitEmptyParameterList);
    vector<TypePtr> Tys;
    for (auto &&Return : Result.ReturnParameters->getParams())
      Tys.push_back(Return->GetType());
    Actions.SetFunRtnTys(Tys);
  } else {
    Result.ReturnParameters =
        make_unique<ParamList>(std::vector<std::unique_ptr<VarDecl>>());
  }

  return Result;
}

void Parser::LexedMethod::ParseLexedMethodDefs() {
  Self->ParseLexedMethodDef(*this);
}

void Parser::ParseLexedMethodDef(LexedMethod &LM) {
  {
    Token BodyEnd;
    BodyEnd.setKind(tok::eof);
    BodyEnd.setLocation(LM.Toks.back().getEndLoc());
    LM.Toks.emplace_back(std::move(BodyEnd));
    // Append the current token at the end of the new token stream so that it
    // doesn't get lost.
    LM.Toks.emplace_back(Tok);
  }
  TheLexer.EnterTokenStream(LM.Toks.data(), LM.Toks.size());

  // Consume the previously pushed token.
  ConsumeAnyToken();

  auto *FD = dynamic_cast<FunctionDecl *>(LM.D);
  ParseScope ArgumentScope{this, 0};
  for (auto *P : FD->getParams()->getParams()) {
    Actions.addDecl(P);
  }
  {
    vector<TypePtr> Tys;
    for (auto &&Return : FD->getReturnParams()->getParams())
      Tys.push_back(Return->GetType());
    Actions.SetFunRtnTys(Tys);
  }

  ParseScope FunctionScope{this, Scope::FunctionScope};
  FD->setBody(parseBlock());
  if (!Tok.is(tok::eof)) {
    assert(false);
  }
  ConsumeToken(); // eof
  Actions.EraseFunRtnTys();
}

unique_ptr<FunctionDecl>
Parser::parseFunctionDefinitionOrFunctionTypeStateVariable() {
  ParseScope ArgumentScope{this, 0};
  FunctionHeaderParserResult Header = parseFunctionHeader(false, true);
  if (Header.IsConstructor || !Header.Modifiers.empty() ||
      !Header.Name.empty() || Tok.isOneOf(tok::semi, tok::l_brace)) {
    // this has to be a function, consume the tokens and store them for later
    // parsing
    auto FD = Actions.CreateFunctionDecl(
        Header.Name, Header.Vsblty, Header.SM, Header.IsConstructor,
        Header.IsFallback, std::move(Header.Parameters),
        std::move(Header.Modifiers), std::move(Header.ReturnParameters),
        nullptr);
    if (Tok.is(tok::l_brace)) {
      auto LM = std::make_unique<LexedMethod>(this, FD.get());
      LM->Toks.push_back(Tok);
      ConsumeBrace();
      if (!ConsumeAndStoreUntil(tok::r_brace, LM->Toks)) {
        assert(false);
      }
      LateParsedDeclarations.emplace_back(std::move(LM));
    } else if (ExpectAndConsumeSemi()) {
      return nullptr;
    }
    return FD;
  } else {
    // TODO: State Variable case.
    return nullptr;
  }
}

bool Parser::ConsumeAndStoreUntil(tok::TokenKind T1, tok::TokenKind T2,
                                  llvm::SmallVector<Token, 4> &Toks) {
  while (true) {
    // If we found one of the tokens, stop and return true.
    if (Tok.is(T1) || Tok.is(T2)) {
      Toks.push_back(Tok);
      ConsumeAnyToken(); // T1 | T2
      return true;
    }

    switch (Tok.getKind()) {
    case tok::eof:
      // Ran out of tokens.
      return false;

    case tok::l_paren:
      // Recursively consume properly-nested parens.
      Toks.push_back(Tok);
      ConsumeParen();
      ConsumeAndStoreUntil(tok::r_paren, Toks);
      break;
    case tok::l_square:
      // Recursively consume properly-nested square brackets.
      Toks.push_back(Tok);
      ConsumeBracket();
      ConsumeAndStoreUntil(tok::r_square, Toks);
      break;
    case tok::l_brace:
      // Recursively consume properly-nested braces.
      Toks.push_back(Tok);
      ConsumeBrace();
      ConsumeAndStoreUntil(tok::r_brace, Toks);
      break;

    case tok::r_paren:
      Toks.push_back(Tok);
      ConsumeParen();
      break;

    case tok::r_square:
      Toks.push_back(Tok);
      ConsumeBracket();
      break;

    case tok::r_brace:
      Toks.push_back(Tok);
      ConsumeBrace();
      break;

    default:
      // consume this token.
      Toks.push_back(Tok);
      ConsumeAnyToken();
      break;
    }
  }
}

unique_ptr<VarDecl>
Parser::parseVariableDeclaration(VarDeclParserOptions const &Options,
                                 TypePtr &&LookAheadArrayType) {
  TypePtr T;
  if (LookAheadArrayType) {
    T = std::move(LookAheadArrayType);
  } else {
    T = parseTypeName(Options.AllowVar);
  }

  bool IsIndexed = false;
  bool IsDeclaredConst = false;
  Decl::Visibility Vsblty = Decl::Visibility::Default;
  VarDecl::Location Loc = VarDecl::Location::Unspecified;
  llvm::StringRef Name;
  while (!Tok.is(tok::eof)) {
    if (Options.IsStateVariable &&
        Tok.isOneOf(tok::kw_public, tok::kw_private, tok::kw_internal)) {
      Vsblty = parseVisibilitySpecifier();
    } else if (Options.AllowIndexed && Tok.is(tok::kw_indexed)) {
      IsIndexed = true;
      ConsumeToken(); // 'indexed'
    } else if (Tok.is(tok::kw_constant)) {
      IsDeclaredConst = true;
      ConsumeToken(); // 'constant'
    } else if (Options.AllowLocationSpecifier &&
               Tok.isOneOf(tok::kw_memory, tok::kw_storage, tok::kw_calldata)) {
      if (Loc != VarDecl::Location::Unspecified) {
        Diag(diag::err_multiple_variable_location);
        return nullptr;
      }
      if (!T) {
        Diag(diag::err_location_without_typename);
        return nullptr;
      }
      switch (Tok.getKind()) {
      case tok::kw_storage:
        Loc = VarDecl::Location::Storage;
        ConsumeToken(); // 'storage'
        break;
      case tok::kw_memory:
        Loc = VarDecl::Location::Memory;
        ConsumeToken(); // 'memory'
        break;
      case tok::kw_calldata:
        Loc = VarDecl::Location::CallData;
        ConsumeToken(); // 'calldata'
        break;
      default:
        __builtin_unreachable();
      }
    } else {
      break;
    }
  }

  if (Options.AllowEmptyName && !Tok.isAnyIdentifier()) {
    Name = llvm::StringRef("");
  } else {
    Name = Tok.getIdentifierInfo()->getName();
    ConsumeToken();
  }

  unique_ptr<Expr> Value;
  if (Options.AllowInitialValue) {
    if (TryConsumeToken(tok::equal)) {
      Value = parseExpression();
    }
  }

  auto VD = std::make_unique<VarDecl>(std::move(T), Name, std::move(Value),
                                      Vsblty, Options.IsStateVariable,
                                      IsIndexed, IsDeclaredConst, Loc);

  Actions.addDecl(VD.get());
  return VD;
}

unique_ptr<EventDecl> Parser::parseEventDefinition() {
  const std::string Name = Tok.getIdentifierInfo()->getName();
  ConsumeToken(); // identifier
  VarDeclParserOptions Options;
  Options.AllowIndexed = true;
  std::unique_ptr<ParamList> Parameters = parseParameterList(Options);
  bool Anonymous = false;
  if (TryConsumeToken(tok::kw_anonymous)) {
    Anonymous = true;
  }
  if (ExpectAndConsumeSemi()) {
    return nullptr;
  }
  auto ED = Actions.CreateEventDecl(Name, std::move(Parameters), Anonymous);
  Actions.addDecl(ED.get());
  return ED;
}

TypePtr Parser::parseTypeNameSuffix(TypePtr T) {
  while (TryConsumeToken(tok::l_square)) {
    if (Tok.is(tok::numeric_constant)) {
      int NumValue;
      if (llvm::StringRef(Tok.getLiteralData(), Tok.getLength())
              .getAsInteger(0, NumValue)) {
        assert(false && "invalid array length");
        __builtin_unreachable();
      }
      ConsumeToken();
      if (ExpectAndConsume(tok::r_square)) {
        return nullptr;
      }
      T = make_shared<ArrayType>(std::move(T), NumValue, parseDataLocation());
    } else {
      if (ExpectAndConsume(tok::r_square)) {
        return nullptr;
      }
      T = make_shared<ArrayType>(std::move(T), parseDataLocation());
    }
  }
  return T;
}

// TODO: < Need complete all Types >
TypePtr Parser::parseTypeName(bool AllowVar) {
  TypePtr T;
  bool HaveType = false;
  const tok::TokenKind Kind = Tok.getKind();
  if (Tok.isElementaryTypeName()) {
    if (Kind == tok::kw_bool) {
      T = std::make_shared<BooleanType>();
      ConsumeToken(); // 'bool'
    } else if (tok::kw_int <= Kind && Kind <= tok::kw_uint256) {
      T = std::make_shared<IntegerType>(token2inttype(Tok));
      ConsumeToken(); // int or uint
    } else if (tok::kw_bytes1 <= Kind && Kind <= tok::kw_bytes32) {
      T = std::make_shared<FixedBytesType>(token2bytetype(Tok));
      ConsumeToken(); // fixedbytes
    } else if (Kind == tok::kw_bytes) {
      T = std::make_shared<BytesType>();
      ConsumeToken(); // 'bytes'
    } else if (Kind == tok::kw_string) {
      T = std::make_shared<StringType>();
      ConsumeToken(); // 'string'
    } else if (Kind == tok::kw_address) {
      ConsumeToken(); // 'address'
      StateMutability SM = StateMutability::NonPayable;
      if (Tok.isOneOf(tok::kw_constant, tok::kw_pure, tok::kw_view,
                      tok::kw_payable)) {
        SM = parseStateMutability();
      }
      T = std::make_shared<AddressType>(SM);
    }
    HaveType = true;
  } else if (Kind == tok::kw_var) {
    // TODO: parseTypeName tok::kw_var (var is deprecated)
    Diag(diag::err_unimplemented_token) << tok::kw_var;
    return nullptr;
  } else if (Kind == tok::kw_function) {
    // TODO: parseTypeName tok::kw_function
    Diag(diag::err_unimplemented_token) << tok::kw_function;
    return nullptr;
  } else if (Kind == tok::kw_mapping) {
    T = parseMapping();
  } else if (Kind == tok::identifier || Kind == tok::raw_identifier) {
    // TODO: parseTypeName tok::identifier
    Diag(diag::err_unimplemented_token) << Kind;
    return nullptr;
  } else {
    assert(false && "Expected Type Name");
    return nullptr;
  }

  if (T || HaveType) {
    T = parseTypeNameSuffix(move(T));
  }
  return T;
}

shared_ptr<MappingType> Parser::parseMapping() {
  if (ExpectAndConsume(tok::kw_mapping)) {
    return nullptr;
  }
  if (ExpectAndConsume(tok::l_paren)) {
    return nullptr;
  }
  bool const AllowVar = false;
  TypePtr KeyType;
  if (Tok.isElementaryTypeName()) {
    KeyType = parseTypeName(AllowVar);
  }
  if (ExpectAndConsume(tok::equalgreater)) {
    return nullptr;
  }
  TypePtr ValueType = parseTypeName(AllowVar);
  if (ExpectAndConsume(tok::r_paren)) {
    return nullptr;
  }
  return std::make_shared<MappingType>(std::move(KeyType),
                                       std::move(ValueType));
}

unique_ptr<ParamList>
Parser::parseParameterList(VarDeclParserOptions const &_Options,
                           bool AllowEmpty) {
  vector<unique_ptr<VarDecl>> Parameters;
  VarDeclParserOptions Options(_Options);
  Options.AllowEmptyName = true;
  if (ExpectAndConsume(tok::l_paren)) {
    return nullptr;
  }
  if (!AllowEmpty || Tok.isNot(tok::r_paren)) {
    Parameters.push_back(parseVariableDeclaration(Options));
    while (Tok.isNot(tok::r_paren)) {
      if (ExpectAndConsume(tok::comma)) {
        return nullptr;
      }
      Parameters.push_back(parseVariableDeclaration(Options));
    }
  }
  if (ExpectAndConsume(tok::r_paren)) {
    return nullptr;
  }

  return std::make_unique<ParamList>(std::move(Parameters));
}

unique_ptr<Block> Parser::parseBlock() {
  ParseScope BlockScope{this, 0};
  vector<unique_ptr<Stmt>> Statements;
  if (ExpectAndConsume(tok::l_brace)) {
    return nullptr;
  }
  while (Tok.isNot(tok::r_brace)) {
    auto Statement = parseStatement();
    if (!Statement) {
      break;
    }
    Statements.emplace_back(std::move(Statement));
  }
  ConsumeBrace(); // '}'
  return std::make_unique<Block>(std::move(Statements));
}

// TODO: < Parse all statements >
unique_ptr<Stmt> Parser::parseStatement() {
  unique_ptr<Stmt> Statement;
  switch (Tok.getKind()) {
  case tok::kw_if:
    return parseIfStatement();
  case tok::kw_while:
    return parseWhileStatement();
  case tok::kw_do:
    return parseDoWhileStatement();
  case tok::kw_for:
    return parseForStatement();
  case tok::l_brace:
    return parseBlock();
  case tok::kw_continue:
    ConsumeToken(); // 'continue'
    Statement = std::make_unique<ContinueStmt>();
    break;
  case tok::kw_break:
    ConsumeToken(); // 'break'
    Statement = std::make_unique<BreakStmt>();
    break;
  case tok::kw_return:
    ConsumeToken(); // 'return'
    if (Tok.isNot(tok::semi)) {
      Statement = Actions.CreateReturnStmt(parseExpression());
    } else {
      Statement = Actions.CreateReturnStmt();
    }
    break;
  case tok::kw_assembly:
    // TODO: parseStatement kw_assembly
    ConsumeToken(); // 'assembly'
    break;
  case tok::kw_emit:
    Statement = parseEmitStatement();
    break;
  case tok::identifier:
  case tok::raw_identifier:
  default:
    Statement = parseSimpleStatement();
    break;
  }
  if (ExpectAndConsumeSemi()) {
    return nullptr;
  }
  return Statement;
}

unique_ptr<IfStmt> Parser::parseIfStatement() {
  ConsumeToken(); // 'if'
  if (ExpectAndConsume(tok::l_paren)) {
    return nullptr;
  }
  unique_ptr<Expr> Condition = parseExpression();
  if (ExpectAndConsume(tok::r_paren)) {
    return nullptr;
  }
  unique_ptr<Stmt> TrueBody = parseStatement();
  unique_ptr<Stmt> FalseBody;
  if (TryConsumeToken(tok::kw_else)) {
    FalseBody = parseStatement();
  }
  return std::make_unique<IfStmt>(std::move(Condition), std::move(TrueBody),
                                  std::move(FalseBody));
}

unique_ptr<WhileStmt> Parser::parseWhileStatement() {
  ConsumeToken(); // 'while'
  if (ExpectAndConsume(tok::l_brace)) {
    return nullptr;
  }
  unique_ptr<Expr> Condition = parseExpression();
  if (ExpectAndConsume(tok::r_brace)) {
    return nullptr;
  }
  unique_ptr<Stmt> Body;
  {
    ParseScope WhileScope{this, Scope::BreakScope | Scope::ContinueScope};
    Body = parseStatement();
  }
  return std::make_unique<WhileStmt>(std::move(Condition), std::move(Body),
                                     false);
}

unique_ptr<WhileStmt> Parser::parseDoWhileStatement() {
  ConsumeToken(); // 'do'
  unique_ptr<Stmt> Body;
  {
    ParseScope DoWhileScope{this, Scope::BreakScope | Scope::ContinueScope};
    Body = parseStatement();
  }
  if (ExpectAndConsume(tok::kw_while)) {
    return nullptr;
  }
  if (ExpectAndConsume(tok::l_brace)) {
    return nullptr;
  }
  unique_ptr<Expr> Condition = parseExpression();
  if (ExpectAndConsume(tok::r_brace)) {
    return nullptr;
  }
  ExpectAndConsumeSemi();
  return std::make_unique<WhileStmt>(std::move(Condition), std::move(Body),
                                     true);
}

unique_ptr<ForStmt> Parser::parseForStatement() {
  ConsumeToken(); // 'for'
  if (ExpectAndConsume(tok::l_paren)) {
    return nullptr;
  }

  // TODO: Maybe here have some predicate like peekExpression() instead of
  // checking for semicolon and RParen?
  unique_ptr<Stmt> Init;
  if (Tok.isNot(tok::semi)) {
    Init = parseSimpleStatement();
  }
  ExpectAndConsumeSemi();

  unique_ptr<Expr> Condition;
  if (Tok.isNot(tok::semi)) {
    Condition = parseExpression();
  }
  ExpectAndConsumeSemi();

  unique_ptr<Expr> Loop;
  if (Tok.isNot(tok::r_paren)) {
    Loop = parseExpression();
  }
  ExpectAndConsume(tok::r_paren);

  unique_ptr<Stmt> Body;
  {
    ParseScope ForScope{this, Scope::BreakScope | Scope::ContinueScope};
    Body = parseStatement();
  }
  return std::make_unique<ForStmt>(std::move(Init), std::move(Condition),
                                   std::move(Loop), std::move(Body));
}

unique_ptr<EmitStmt> Parser::parseEmitStatement() {
  ConsumeToken(); // 'emit'

  IndexAccessedPath Iap;
  while (true) {
    if (Tok.isNot(tok::identifier)) {
      Diag(diag::err_expected_event);
      return nullptr;
    }
    Iap.Path.emplace_back(Tok);
    ConsumeToken(); // identifier
    if (Tok.isNot(tok::period))
      break;
    ConsumeToken(); // '.'
  };

  auto EventName = expressionFromIndexAccessStructure(Iap);

  if (ExpectAndConsume(tok::l_paren)) {
    return nullptr;
  }
  vector<unique_ptr<Expr>> Arguments;
  vector<llvm::StringRef> Names;
  tie(Arguments, Names) = parseFunctionCallArguments();
  if (ExpectAndConsume(tok::r_paren)) {
    return nullptr;
  }
  unique_ptr<CallExpr> Call =
      Actions.CreateCallExpr(std::move(EventName), std::move(Arguments));
  return make_unique<EmitStmt>(std::move(Call));
}

unique_ptr<Stmt> Parser::parseSimpleStatement() {
  llvm::Optional<Token> CurTok;
  LookAheadInfo StatementType;
  IndexAccessedPath Iap;
  unique_ptr<Expr> Expression;

  bool IsParenExpr = false;
  if (TryConsumeToken(tok::l_paren)) {
    IsParenExpr = true;
  }

  tie(StatementType, Iap) = tryParseIndexAccessedPath();
  switch (StatementType) {
  case LookAheadInfo::VariableDeclaration:
    return parseVariableDeclarationStatement(
        typeNameFromIndexAccessStructure(Iap));
  case LookAheadInfo::Expression:
    Expression = parseExpression(expressionFromIndexAccessStructure(Iap));
    break;
  default:
    assert(false && "Unhandle statement.");
  }
  if (IsParenExpr) {
    if (ExpectAndConsume(tok::r_paren)) {
      return nullptr;
    }
    return parseExpression(std::make_unique<ParenExpr>(std::move(Expression)));
  }
  return Expression;
}

unique_ptr<DeclStmt>
Parser::parseVariableDeclarationStatement(TypePtr &&LookAheadArrayType) {
  // This does not parse multi variable declaration statements starting directly
  // with
  // `(`, they are parsed in parseSimpleStatement, because they are hard to
  // distinguish from tuple expressions.
  vector<unique_ptr<VarDecl>> Variables;
  unique_ptr<Expr> Value;
  if (!LookAheadArrayType && Tok.is(tok::kw_var) &&
      NextToken().is(tok::l_paren)) {
    // [0.4.20] The var keyword has been deprecated for security reasons.
    // https://github.com/ethereum/solidity/releases/tag/v0.4.20
    Diag(diag::err_unimplemented_token) << tok::kw_var;
    return nullptr;
  } else {
    VarDeclParserOptions Options;
    Options.AllowVar = false;
    Options.AllowLocationSpecifier = true;
    Variables.push_back(
        parseVariableDeclaration(Options, std::move(LookAheadArrayType)));
  }
  if (TryConsumeToken(tok::equal)) {
    Value = parseExpression();
  }

  return std::make_unique<DeclStmt>(std::move(Variables), std::move(Value));
}

bool Parser::IndexAccessedPath::empty() const {
  if (!Indices.empty()) {
    assert(!(Path.empty() && ElementaryType) && "");
  }
  return Path.empty() && (ElementaryType == nullptr) && Indices.empty();
}

pair<Parser::LookAheadInfo, Parser::IndexAccessedPath>
Parser::tryParseIndexAccessedPath() {
  // These two cases are very hard to distinguish:
  // x[7 * 20 + 3] a;     and     x[7 * 20 + 3] = 9;
  // In the first case, x is a type name, in the second it is the name of a
  // variable. As an extension, we can even have: `x.y.z[1][2] a;` and
  // `x.y.z[1][2] = 10;` Where in the first, x.y.z leads to a type name where in
  // the second, it accesses structs.

  auto StatementType = peekStatementType();

  switch (StatementType) {
  case LookAheadInfo::VariableDeclaration:
  case LookAheadInfo::Expression:
    return make_pair(StatementType, IndexAccessedPath());
  default:
    break;
  }
  // At this point, we have 'Identifier "["' or 'Identifier "." Identifier' or
  // 'ElementoryTypeName "["'. We parse '(Identifier ("." Identifier)*
  // |ElementaryTypeName) ( "[" Expression "]" )*' until we can decide whether
  // to hand this over to ExpressionStatement or create a
  // VariableDeclarationStatement out of it.
  IndexAccessedPath Iap = parseIndexAccessedPath();

  if (Tok.isOneOf(tok::identifier, tok::kw_memory, tok::kw_storage,
                  tok::kw_calldata))
    return make_pair(LookAheadInfo::VariableDeclaration, move(Iap));
  else
    return make_pair(LookAheadInfo::Expression, move(Iap));
}

Parser::LookAheadInfo Parser::peekStatementType() const {
  // Distinguish between variable declaration (and potentially assignment) and
  // expression statement (which include assignments to other expressions and
  // pre-declared variables). We have a variable declaration if we get a keyword
  // that specifies a type name. If it is an identifier or an elementary type
  // name followed by an identifier or a mutability specifier, we also have a
  // variable declaration. If we get an identifier followed by a "[" or ".", it
  // can be both ("lib.type[9] a;" or "variable.el[9] = 7;"). In all other
  // cases, we have an expression statement.
  if (Tok.isOneOf(tok::kw_mapping, tok::kw_function, tok::kw_var)) {
    return LookAheadInfo::VariableDeclaration;
  }

  bool MightBeTypeName = Tok.isElementaryTypeName() || Tok.is(tok::identifier);

  if (MightBeTypeName) {
    const Token &NextTok = NextToken();
    // So far we only allow ``address payable`` in variable declaration
    // statements and in no other kind of statement. This means, for example,
    // that we do not allow type expressions of the form
    // ``address payable;``.
    // If we want to change this in the future, we need to consider another
    // scanner token here.
    if (Tok.isElementaryTypeName() &&
        NextTok.isOneOf(tok::kw_pure, tok::kw_view, tok::kw_payable)) {
      return LookAheadInfo::VariableDeclaration;
    }
    if (NextTok.isOneOf(tok::raw_identifier, tok::identifier, tok::kw_memory,
                        tok::kw_storage, tok::kw_calldata)) {
      return LookAheadInfo::VariableDeclaration;
    }
    if (NextTok.isOneOf(tok::l_square, tok::period)) {
      return LookAheadInfo::IndexAccessStructure;
    }
  }
  return LookAheadInfo::Expression;
}

Parser::IndexAccessedPath Parser::parseIndexAccessedPath() {
  IndexAccessedPath Iap;
  if (Tok.isAnyIdentifier()) {
    do {
      Iap.Path.emplace_back(Tok);
      ConsumeToken(); // identifier
    } while (TryConsumeToken(tok::period));
  } else {
    Iap.ElementaryType = parseTypeName(false);
  }

  while (Tok.is(tok::l_square)) {
    ConsumeBracket();
    Iap.Indices.emplace_back(parseExpression());
    if (ExpectAndConsume(tok::r_square)) {
      break;
    }
  }

  return Iap;
}

// TODO: IAP relative function
TypePtr
Parser::typeNameFromIndexAccessStructure(Parser::IndexAccessedPath &Iap) {
  if (Iap.empty())
    return {};

  TypePtr T;

  if (Iap.ElementaryType != nullptr) {
    T = std::move(Iap.ElementaryType);
  } else {
    // TODO: UserDefinedTypeName
    // T = UserDefinedTypeName with Path
  }
  for (auto &Length : Iap.Indices) {
    T = make_shared<ArrayType>(
        std::move(T),
        dynamic_cast<const NumberLiteral *>(Length.get())->getValue(),
        parseDataLocation());
  }
  return T;
}

// TODO: IAP relative function
unique_ptr<Expr>
Parser::expressionFromIndexAccessStructure(Parser::IndexAccessedPath &Iap) {
  if (Iap.empty()) {
    return nullptr;
  }
  unique_ptr<Expr> Expression = Actions.CreateIdentifier(Iap.Path.front());
  if (!Expression) {
    return nullptr;
  }
  for (size_t i = 1; i < Iap.Path.size(); ++i) {
    Expression =
        Actions.CreateMemberExpr(std::move(Expression), std::move(Iap.Path[i]));
    if (!Expression) {
      return nullptr;
    }
  }

  for (auto &Index : Iap.Indices) {
    Expression =
        Actions.CreateIndexAccess(std::move(Expression), std::move(Index));
    if (!Expression) {
      return nullptr;
    }
  }
  return Expression;
}

unique_ptr<Expr>
Parser::parseExpression(unique_ptr<Expr> &&PartiallyParsedExpression) {
  unique_ptr<Expr> Expression =
      parseBinaryExpression(4, std::move(PartiallyParsedExpression));

  if (tok::equal <= Tok.getKind() && Tok.getKind() < tok::percentequal) {
    const BinaryOperatorKind Op = token2bop(Tok);
    ConsumeToken();
    unique_ptr<Expr> RightHandSide = parseExpression();
    return std::move(Actions.CreateBinOp(Op, std::move(Expression),
                                         std::move(RightHandSide)));
  }

  if (TryConsumeToken(tok::question)) {
    unique_ptr<Expr> trueExpression = parseExpression();
    if (ExpectAndConsume(tok::colon)) {
      return nullptr;
    }
    unique_ptr<Expr> falseExpression = parseExpression();
    // TODO: Create ConditionExpression
    return nullptr;
  }

  return Expression;
}

unique_ptr<Expr>
Parser::parseBinaryExpression(int MinPrecedence,
                              unique_ptr<Expr> &&PartiallyParsedExpression) {
  unique_ptr<Expr> Expression =
      parseUnaryExpression(std::move(PartiallyParsedExpression));
  int Precedence = static_cast<int>(getBinOpPrecedence(Tok.getKind()));
  for (; Precedence >= MinPrecedence; --Precedence) {
    while (getBinOpPrecedence(Tok.getKind()) == Precedence) {
      const BinaryOperatorKind Op = token2bop(Tok);
      ConsumeToken(); // binary op
      unique_ptr<Expr> RightHandSide = parseBinaryExpression(Precedence + 1);
      Expression = std::move(Actions.CreateBinOp(Op, std::move(Expression),
                                                 std::move(RightHandSide)));
    }
  }
  return Expression;
}

unique_ptr<Expr>
Parser::parseUnaryExpression(unique_ptr<Expr> &&PartiallyParsedExpression) {
  UnaryOperatorKind Op = token2uop(Tok);

  if (!PartiallyParsedExpression && Tok.isUnaryOp()) {
    ConsumeToken(); // pre '++' or '--'
    unique_ptr<Expr> SubExps = parseUnaryExpression();
    return std::make_unique<UnaryOperator>(std::move(SubExps), Op,
                                           SubExps->getType());
  } else {
    // potential postfix expression
    unique_ptr<Expr> SubExps =
        parseLeftHandSideExpression(std::move(PartiallyParsedExpression));
    Op = token2uop(Tok, false);
    if (!(Op == UnaryOperatorKind::UO_PostInc ||
          Op == UnaryOperatorKind::UO_PostDec))
      return SubExps;
    ConsumeToken(); // post '++' or '--'
    return std::make_unique<UnaryOperator>(std::move(SubExps), Op,
                                           SubExps->getType());
  }
}

unique_ptr<Expr> Parser::parseLeftHandSideExpression(
    unique_ptr<Expr> &&PartiallyParsedExpression) {
  unique_ptr<Expr> Expression;
  if (PartiallyParsedExpression)
    Expression = std::move(PartiallyParsedExpression);
  else if (TryConsumeToken(tok::kw_new)) {
    TypePtr typeName = parseTypeName(false);
    // [AST] create NewExpression
  } else
    Expression = std::move(parsePrimaryExpression());

  while (true) {
    switch (Tok.getKind()) {
    case tok::l_square: {
      ConsumeBracket(); // '['
      unique_ptr<Expr> Index;
      if (Tok.isNot(tok::r_square))
        Index = std::move(parseExpression());
      if (ExpectAndConsume(tok::r_square)) {
        return nullptr;
      }
      Expression =
          Actions.CreateIndexAccess(std::move(Expression), std::move(Index));
      break;
    }
    case tok::period: {
      ConsumeToken(); // '.'
      if (!Tok.isAnyIdentifier()) {
        Diag(diag::err_expected) << tok::identifier;
      }
      Expression = Actions.CreateMemberExpr(std::move(Expression), Tok);
      if (!Expression) {
        return nullptr;
      }
      ConsumeToken(); // identifier
      break;
    }
    case tok::l_paren: {
      ConsumeParen(); // '('
      vector<unique_ptr<Expr>> Arguments;
      vector<llvm::StringRef> Names;
      tie(Arguments, Names) = parseFunctionCallArguments();
      if (ExpectAndConsume(tok::r_paren)) {
        return nullptr;
      }
      // TODO: Fix passs arguments' name fail.
      Expression =
          Actions.CreateCallExpr(std::move(Expression), std::move(Arguments));
      break;
    }
    default:
      return Expression;
    }
  }
}

unique_ptr<Expr> Parser::parsePrimaryExpression() {
  unique_ptr<Expr> Expression;

  // Explicit Type Casting
  if (Tok.isElementaryTypeName() && NextToken().is(tok::l_paren)) {
    const auto TypeNameTok = Tok;
    ConsumeToken(); // elementary typename
    ConsumeParen(); // '('
    if (TypeNameTok.is(tok::kw_address)) {
      Expression = make_unique<ExplicitCastExpr>(
          parseExpression(), CastKind::TypeCast,
          make_shared<AddressType>(StateMutability::Payable));
    } else {
      Expression = make_unique<ExplicitCastExpr>(
          parseExpression(), CastKind::IntegralCast,
          make_shared<IntegerType>(token2inttype(TypeNameTok)));
    }
    if (ExpectAndConsume(tok::r_paren)) {
      return nullptr;
    }
    return Expression;
  }

  const auto Kind = Tok.getKind();
  switch (Kind) {
  case tok::kw_true:
    ConsumeToken(); // 'true'
    Expression = std::make_unique<BooleanLiteral>(true);
    break;
  case tok::kw_false:
    ConsumeToken(); // 'false'
    Expression = std::make_unique<BooleanLiteral>(false);
    break;
  case tok::numeric_constant: {
    int NumValue;
    if (llvm::StringRef(Tok.getLiteralData(), Tok.getLength())
            .getAsInteger(0, NumValue)) {
      assert(false && "invalid numeric constant");
      __builtin_unreachable();
    }
    Expression = std::make_unique<NumberLiteral>(NumValue);
    ConsumeToken(); // numeric constant
    break;
  }
  case tok::string_literal: {
    std::string StrValue(Tok.getLiteralData(), Tok.getLength());
    Expression = make_unique<StringLiteral>(stringUnquote(std::move(StrValue)));
    ConsumeStringToken(); // string literal
    break;
  }
  case tok::hex_string_literal: {
    std::string StrValue(Tok.getLiteralData(), Tok.getLength());
    Expression = make_unique<StringLiteral>(hexUnquote(std::move(StrValue)));
    ConsumeStringToken(); // hex string literal
    break;
  }
  case tok::identifier: {
    Expression = Actions.CreateIdentifier(Tok);
    ConsumeToken(); // identifier
    break;
  }
  case tok::kw_type:
    // TODO: Type expression is globally-avariable function
    Diag(diag::err_unimplemented_token) << tok::kw_type;
    return nullptr;
  case tok::l_paren:
  case tok::l_square: {
    // TODO: Tuple case
    //
    // Tuple/parenthesized expression or inline array/bracketed expression.
    // Special cases: ()/[] is empty tuple/array type, (x) is not a real tuple,
    // (x,) is one-dimensional tuple, elements in arrays cannot be left out,
    // only in tuples.
    const tok::TokenKind OppositeKind =
        (Kind == tok::l_paren ? tok::r_paren : tok::r_square);
    ConsumeAnyToken(); // '[' or '('
    Expression = std::make_unique<ParenExpr>(parseExpression());
    ExpectAndConsume(OppositeKind);
    break;
  }
  default:
    // TODO: Type MxN case
    assert(false && "Unknown token");
    return nullptr;
  }
  return Expression;
}

vector<unique_ptr<Expr>> Parser::parseFunctionCallListArguments() {
  vector<unique_ptr<Expr>> Arguments;
  if (Tok.isNot(tok::r_paren)) {
    Arguments.push_back(parseExpression());
    while (Tok.isNot(tok::r_paren)) {
      if (ExpectAndConsume(tok::comma)) {
        return Arguments;
      }
      Arguments.push_back(parseExpression());
    }
  }
  return Arguments;
}

pair<vector<unique_ptr<Expr>>, vector<llvm::StringRef>>
Parser::parseFunctionCallArguments() {
  pair<vector<unique_ptr<Expr>>, vector<llvm::StringRef>> Ret;
  if (Tok.is(tok::l_brace)) {
    // TODO: Unverified function parameters case
    // call({arg1 : 1, arg2 : 2 })
    ConsumeBrace();
    bool First = true;
    while (Tok.isNot(tok::r_brace)) {
      if (!First) {
        if (ExpectAndConsume(tok::comma)) {
          return Ret;
        }
      }

      if (Tok.isNot(tok::identifier)) {
        Diag(diag::err_expected) << tok::identifier;
        return Ret;
      }
      Ret.second.emplace_back(Tok.getIdentifierInfo()->getName());
      ConsumeToken(); // identifier
      Ret.first.emplace_back(parseExpression());

      if (Tok.is(tok::comma) && NextToken().is(tok::r_brace)) {
        Diag(diag::err_trailing_comma);
        ConsumeToken(); // ','
      }
      First = false;
    }
    ConsumeToken(); // ')'
  } else {
    Ret.first = std::move(parseFunctionCallListArguments());
  }
  return Ret;
}

void Parser::EnterScope(unsigned ScopeFlags) { Actions.PushScope(ScopeFlags); }

void Parser::ExitScope() { Actions.PopScope(); }

bool Parser::ExpectAndConsumeSemi(unsigned DiagID) {
  if (TryConsumeToken(tok::semi))
    return false;

  if ((Tok.is(tok::r_paren) || Tok.is(tok::r_square)) &&
      NextToken().is(tok::semi)) {
    Diag(diag::err_extraneous_token_before_semi);
    ConsumeAnyToken(); // The ')' or ']'.
    ConsumeToken();    // The ';'.
    return false;
  }

  return ExpectAndConsume(tok::semi, DiagID);
}

bool Parser::ExpectAndConsume(tok::TokenKind ExpectedTok, unsigned DiagID,
                              llvm::StringRef Msg) {
  if (Tok.is(ExpectedTok)) {
    ConsumeAnyToken();
    return false;
  }

  // TODO: Detect common single-character typos and resume.

  DiagnosticBuilder DB = Diag(DiagID);
  if (DiagID == diag::err_expected)
    DB << ExpectedTok;
  else if (DiagID == diag::err_expected_after)
    DB << Msg << ExpectedTok;
  else
    DB << Msg;

  return true;
}

DiagnosticBuilder Parser::Diag(SourceLocation Loc, unsigned DiagID) {
  return Diags.Report(Loc, DiagID);
}

} // namespace soll
