#include "llvm/ADT/STLExtras.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

/*
  >>> grammar tree:

	top:
	    definition
		external
		expression
		';'

	definition:
		'def' prototype expression

	external:
	    'extern' prototype
    
    toplevelexpr:
        expression
    
	expression:
	    primary binoprhs

	primary:
	    identifierexpr
		numberexpr
		parenexpr

	numberexpr:
	    number

	parenexpr:
	    '(' expression ')'

	identifierexpr:
	    identifier
		identifier '(' expression* ')'

	binoprhs:
	    ('+' primary)*

	prototype:
	    identifier '(' identifier* ')'

  >>> usage:
	ready> def foo(x y) x+ foo(y, 4.0);
	ready> def foo(x y) x+ y y;
	ready> def foo(x y) x+y );
	ready> extern sin(a);
	ready> ^D
*/

//  lexer
enum Token {
	tok_eof = -1,
	tok_def = -2,
	tok_extern = -3,
	tok_identifier = -4,
	tok_number = -5
};

static std::string IdentifierStr;  // filled in if tok_identifier
static double NumVal;  // filled in if tok_number

// return the next token from standard input
static int gettok() {
	static int LastChar = ' ';

	while (isspace(LastChar)) // skip any whitespace
		LastChar = getchar(); // get next char

	if (isalpha(LastChar)) {  // identifier: [a-zA-Z][a-zA-Z0-9]*
		IdentifierStr = LastChar;
		while (isalnum((LastChar = getchar())))
			IdentifierStr += LastChar;

	    if (IdentifierStr == "def")
	    	return tok_def;
	    if (IdentifierStr == "extern")
	    	return tok_extern;
	    return tok_identifier;
	}

	if (isdigit(LastChar) || LastChar == '.') {  // Number: [0-9.]+
		std::string NumStr;
		do {
			NumStr += LastChar;
			LastChar = getchar();
		} while (isdigit(LastChar) || LastChar == '.');
		NumVal = strtod(NumStr.c_str(), nullptr);
		return tok_number;
	}

	if (LastChar == '#') { // comment until end of line
		do
			LastChar = getchar();
		while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
		if (LastChar != EOF)
			return gettok();
	}

	// check for end of file. Don't eat the EOF.
	if (LastChar == EOF)
		return tok_eof;

	// otherwise just return the character as its ascii value
	int ThisChar = LastChar;
	LastChar = getchar();
	return ThisChar;
}


// AST

namespace {

	// base class for all expression nodes
	class ExprAST {
		public:
			virtual ~ExprAST() = default;
	};

	// expression class for numeric literals like "1.0"
	class NumberExprAST : public ExprAST {
		double Val;
		public:
		NumberExprAST(double Val) : Val(Val) {}
	};

	// expression class for referencing a variable, like "a"
	class VariableExprAST : public ExprAST {
		std::string Name;
		public:
		VariableExprAST(const std::string& Name) : Name(Name) {}
	};

	// expression class for a binary operator
	class BinaryExprAST : public ExprAST {
		char Op;
		std::unique_ptr<ExprAST> LHS, RHS;
		public:
		BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
				std::unique_ptr<ExprAST> RHS)
			: Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
	};

	// expression class for function calls
	class CallExprAST : public ExprAST {
		std::string Callee;
		std::vector<std::unique_ptr<ExprAST>> Args;
		public:
		CallExprAST(const std::string& Callee,
				std::vector<std::unique_ptr<ExprAST>> Args)
			: Callee(Callee), Args(std::move(Args)) {}
	};

	// represents the "prototype" of a function, which captures its
	// name, and its argument names (thus implicitly the number of
	// arguments the function takes).
	class PrototypeAST {
		std::string Name;
		std::vector<std::string> Args;
		public:
		PrototypeAST(const std::string& Name, std::vector<std::string> Args)
			: Name(Name), Args(std::move(Args)) {}
		const std::string& getName() const { return Name; }
	};

	// represents a function definition itself
	class FunctionAST {
		std::unique_ptr<PrototypeAST> Proto;
		std::unique_ptr<ExprAST> Body;
		public:
		FunctionAST(std::unique_ptr<PrototypeAST> Proto,
				std::unique_ptr<ExprAST> Body)
			: Proto(std::move(Proto)), Body(std::move(Body)) {}
	};

} // end namespace

// ===============================
//       parser
// ===============================

// CurTok/getNextToken -- Provide a simple token buffer. CurTok is the
// current token the parser is looking at. getNextToken reads another 
// token from the lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

// holds the precedence for each binary operator that is defined
static std::map<char,int> BinopPrecedence;

// get the precedence of the pending binary operator token
static int GetTokPrecedence() {
	if (!isascii(CurTok))
		return -1;

	// make sure it's a declared binop
	int TokPrec = BinopPrecedence[CurTok];
	if (TokPrec <= 0)
		return -1;
	return TokPrec;
}

std::unique_ptr<ExprAST> LogError(const char* Str) {
	fprintf(stderr, "Error: %s\n", Str);
	return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char* Str) {
	LogError(Str);
	return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
	auto Result = llvm::make_unique<NumberExprAST>(NumVal);
	getNextToken();
	return std::move(Result);
}

// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
	getNextToken();
	auto V = ParseExpression();
	if (!V)
		return nullptr;
	if (CurTok != ')')
		return LogError("expected ')'");
	getNextToken();
	return V;
}

//  identifierexpr
//    ::= identifier
//    ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
	std::string IdName = IdentifierStr;
	getNextToken();  // eat identifier

	if (CurTok != '(')  // simple variable ref.
		return llvm::make_unique<VariableExprAST>(IdName);

	// call
	getNextToken(); // eat (
	std::vector<std::unique_ptr<ExprAST>> Args;
	if (CurTok != ')') {
		while (true) {
			if (auto Arg = ParseExpression())
				Args.push_back(std::move(Arg));
			else
				return nullptr;

			if (CurTok == ')')
				break;

			if (CurTok != ',')
				return LogError("Expected ')' or ',' in argument list");
			getNextToken();
		}
	}

	getNextToken(); // eat ')'
	return llvm::make_unique<CallExprAST>(IdName, std::move(Args));
}

// primary
//   ::= identierexpr
//   ::= numberexpr
//   ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
	switch (CurTok) {
	default:
		return LogError("unknown token when expecting an expression");
	case tok_identifier:
		return ParseIdentifierExpr();
	case tok_number:
		return ParseNumberExpr();
	case '(':
		return ParseParenExpr();
	}
}

// binoprhs
//   ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
	std::unique_ptr<ExprAST> LHS) {
	while (true) {
		int TokPrec = GetTokPrecedence();
		if (TokPrec < ExprPrec)
			return LHS;

		int BinOp = CurTok;
		getNextToken();

		auto RHS = ParsePrimary();
		if (!RHS) return nullptr;

		int NextPrec = GetTokPrecedence();
		if (TokPrec < NextPrec) {
			RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
			if (!RHS) return nullptr;
		}

		LHS = llvm::make_unique<BinaryExprAST>(BinOp, std::move(LHS),
			std::move(RHS));
	}
}

// expression 
//   ::= primary binoprhs
static std::unique_ptr<ExprAST> ParseExpression() {
	auto LHS = ParsePrimary();
	if (!LHS) return nullptr;
	return ParseBinOpRHS(0, std::move(LHS));
}

// prototype
//   ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
	if (CurTok != tok_identifier)
		return LogErrorP("Expected function name in prototype");
	std::string FnName = IdentifierStr;
	getNextToken();

	if (CurTok != '(')
		return LogErrorP("Expected '(' in prototype");

	std::vector<std::string> ArgNames;
	while (getNextToken() == tok_identifier)
		ArgNames.push_back(IdentifierStr);
	if (CurTok != ')')
		return LogErrorP("Expected ')' in prototype");

	getNextToken();
	return llvm::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

// defintion
//   ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
	getNextToken();
	auto Proto = ParsePrototype();
	if (!Proto) return nullptr;
	if (auto E = ParseExpression())
		return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
	return nullptr;
}

// toplevelexpr
//   ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
	if (auto E = ParseExpression()) {
		auto Proto = llvm::make_unique<PrototypeAST>("__anon_expr",
			std::vector<std::string>());
		return llvm::make_unique<FunctionAST>(std::move(Proto), std::move(E));
	}
	return nullptr;
}

// external 
//   ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
	getNextToken();
	return ParsePrototype();
}

// ======   top level parsing
static void HandleDefinition() {
	if (ParseDefinition()) {
		fprintf(stderr, "Parsed a function definition.\n");
	}
	else {
		getNextToken();
	}
}

static void HandleExtern() {
	if (ParseExtern()) {
		fprintf(stderr, "Parsed an extern\n");
	}
	else {
		getNextToken();
	}
}

static void HandleTopLevelExpression() {
	if (ParseTopLevelExpr()) {
		fprintf(stderr, "Parsed a top-level expr\n");
	}
	else {
		getNextToken();
	}
}

static void MainLoop() {
	while (true) {
		fprintf(stderr, "ready> ");
		switch (CurTok) {
		case tok_eof:
			return;
		case ';':
			getNextToken();
			break;
		case tok_def:
			HandleDefinition();
			break;
		case tok_extern:
			HandleExtern();
			break;
		default:
			HandleTopLevelExpression();
			break;
		}
	}
}

int main()
{
	BinopPrecedence['<'] = 10;
	BinopPrecedence['+'] = 20;
	BinopPrecedence['-'] = 20;
	BinopPrecedence['*'] = 40; // highest

	fprintf(stderr, "ready> ");
	getNextToken();

	MainLoop();
	return 0;
}
