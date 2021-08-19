#include <cassert>
#include <unordered_map>
#include "GS2CompilerVisitor.h"
#include "ast/ast.h"
#include "Parser.h"

opcode::Opcode getExpressionOpCode(ExpressionOp op)
{
	switch (op)
	{
		case ExpressionOp::Plus: return opcode::Opcode::OP_ADD;
		case ExpressionOp::Minus: return opcode::Opcode::OP_SUB;
		case ExpressionOp::Multiply: return opcode::Opcode::OP_MUL;
		case ExpressionOp::Divide: return opcode::Opcode::OP_DIV;
		case ExpressionOp::Mod: return opcode::Opcode::OP_MOD;
		case ExpressionOp::Pow: return opcode::Opcode::OP_POW;
		case ExpressionOp::Assign: return opcode::Opcode::OP_ASSIGN;
		case ExpressionOp::Equal: return opcode::Opcode::OP_EQ;
		case ExpressionOp::NotEqual: return opcode::Opcode::OP_NEQ;
		case ExpressionOp::LessThan: return opcode::Opcode::OP_LT;
		case ExpressionOp::LessThanOrEqual: return opcode::Opcode::OP_LTE;
		case ExpressionOp::GreaterThan: return opcode::Opcode::OP_GT;
		case ExpressionOp::GreaterThanOrEqual: return opcode::Opcode::OP_GTE;

		case ExpressionOp::PlusAssign: return opcode::Opcode::OP_ADD;
		case ExpressionOp::MinusAssign: return opcode::Opcode::OP_SUB;
		case ExpressionOp::MultiplyAssign: return opcode::Opcode::OP_MUL;
		case ExpressionOp::DivideAssign: return opcode::Opcode::OP_DIV;
		case ExpressionOp::ConcatAssign: return opcode::Opcode::OP_JOIN;

		case ExpressionOp::UnaryMinus: return opcode::Opcode::OP_UNARYSUB;
		case ExpressionOp::UnaryNot: return opcode::Opcode::OP_NOT;
		case ExpressionOp::Increment: return opcode::Opcode::OP_INC;
		case ExpressionOp::Decrement: return opcode::Opcode::OP_DEC;

		default: return opcode::Opcode::OP_NONE;
	}
}

void GS2CompilerVisitor::Visit(Node *node)
{
	auto error_length = std::char_traits<char>::length("Unimplemented node type: ") + strlen(node->NodeType());
	
	std::string errorMsg;
	errorMsg.reserve(error_length);
	errorMsg.append("Unimplemented node type: ").append(node->NodeType());

	parserContext.addError({ GS2CompilerError::ErrorCode::CompileError, errorMsg });

	fprintf(stderr, "%s\n", errorMsg.c_str());

#ifdef _WIN32
	system("pause");
#endif

	exit(1);
}

void GS2CompilerVisitor::Visit(StatementBlock *node)
{
   for (const auto& n : node->statements)
	{
		if (n)
			n->visit(this);
	}
}

void GS2CompilerVisitor::Visit(StatementFnDeclNode *node)
{
#ifdef DBGEMITTERS
	printf("Declare function: %s\n", node->ident.c_str());
#endif

	byteCode.emit(opcode::OP_SET_INDEX);
	byteCode.emit(char(0xF4));
	byteCode.emit(short(0)); // replaced with jump index to last opcode

	std::string funcName;
	if (node->pub)
		funcName.append("public.");
	if (!node->objectName.empty())
		funcName.append(node->objectName).append(".");
	funcName.append(node->ident);

	byteCode.addFunction(funcName, byteCode.getOpIndex(), byteCode.getBytecodePos());

	{
		byteCode.emit(opcode::OP_TYPE_ARRAY);
		
		for (auto it = node->args.rbegin(); it != node->args.rend(); ++it)
		{
			assert(*it);
			
			if (*it != nullptr)
				(*it)->visit(this);
		}
		
		byteCode.emit(opcode::OP_FUNC_PARAMS_END);
	}

	byteCode.emit(opcode::OP_JMP);
	byteCode.emit(opcode::OP_CMD_CALL);

	node->stmtBlock->visit(this);

	// if our last op was a return statement, we can skip writing a duplicate
	// return statement at the end of the function
	if (byteCode.getLastOp() != opcode::OP_RET)
	{
		byteCode.emit(opcode::OP_TYPE_NUMBER);
		byteCode.emitDynamicNumber(0);
		byteCode.emit(opcode::OP_RET);
	}
}

void GS2CompilerVisitor::Visit(ExpressionTernaryOpNode *node)
{
	node->condition->visit(this);
	
	pushLogicalBreakpoint(LogicalBreakPoint{ byteCode.getOpIndex() });
	{
		byteCode.emit(opcode::OP_IF);
		byteCode.emit(char(0xF4));
		byteCode.emit(short(0));
		addLogicalContinueLocation(byteCode.getBytecodePos() - 2);

		node->leftExpr->visit(this);

		// set the continue position to the right-hand expression, skipping
		// over the jump on the left-hand expression
		logicalBreakpoints.top().opcontinue = byteCode.getOpIndex() + 1;
	}
	popLogicalBreakpoint();

	// emit a jump to the end of this else block for the previous if-block
	byteCode.emit(opcode::OP_SET_INDEX);
	byteCode.emit(char(0xF4));
	byteCode.emit(short(0));

	auto elseLoc = byteCode.getBytecodePos() - 2;

	node->rightExpr->visit(this);
	byteCode.emit(short(byteCode.getOpIndex()), elseLoc);
}

void GS2CompilerVisitor::Visit(ExpressionBinaryOpNode *node)
{
	bool handled = false;

	if (node->op == ExpressionOp::LogicalAnd || node->op == ExpressionOp::LogicalOr)
	{
		node->left->visit(this);
		if (node->left->expressionType() != ExpressionType::EXPR_INTEGER)
			byteCode.emit(opcode::OP_CONV_TO_FLOAT);

		if (node->op == ExpressionOp::LogicalAnd)
		{
			byteCode.emit(opcode::OP_IF);
			byteCode.emit(char(0xF4));
			byteCode.emit(short(0));
			
			assert(!logicalBreakpoints.empty());
			addLogicalContinueLocation(byteCode.getBytecodePos() - 2);
		}
		else if (node->op == ExpressionOp::LogicalOr)
		{
			byteCode.emit(opcode::OP_OR);
			byteCode.emit(char(0xF4));
			byteCode.emit(short(0));

			assert(!logicalBreakpoints.empty());
			addLogicalBreakLocation(byteCode.getBytecodePos() - 2);
		}

		node->right->visit(this);
		if (node->right->expressionType() != ExpressionType::EXPR_INTEGER)
			byteCode.emit(opcode::OP_CONV_TO_FLOAT);

		return;
	}

	switch (node->op)
	{
		case ExpressionOp::Plus:
		case ExpressionOp::Minus:
		case ExpressionOp::Multiply:
		case ExpressionOp::Divide:
		case ExpressionOp::Mod:
		case ExpressionOp::Pow:
		case ExpressionOp::LessThan:
		case ExpressionOp::LessThanOrEqual:
		case ExpressionOp::GreaterThan:
		case ExpressionOp::GreaterThanOrEqual:
		{
			node->left->visit(this);
			if (node->left->expressionType() != ExpressionType::EXPR_INTEGER)
				byteCode.emit(opcode::OP_CONV_TO_FLOAT);
			node->right->visit(this);

			auto opCode = getExpressionOpCode(node->op);
			assert(opCode != opcode::Opcode::OP_NONE);

			byteCode.emit(opCode);
			handled = true;
			break;
		}

		case ExpressionOp::Equal:
		case ExpressionOp::NotEqual:
		{
			node->left->visit(this);
			node->right->visit(this);

			auto opCode = getExpressionOpCode(node->op);
			assert(opCode != opcode::Opcode::OP_NONE);

			byteCode.emit(opCode);
			handled = true;
			break;
		}

		case ExpressionOp::PlusAssign:
		case ExpressionOp::MinusAssign:
		case ExpressionOp::MultiplyAssign:
		case ExpressionOp::DivideAssign:
		{
			node->left->visit(this);
			node->left->visit(this);
			byteCode.emitConversionOp(node->left->expressionType(), ExpressionType::EXPR_NUMBER);
			
			auto opCode = getExpressionOpCode(node->op);
			assert(opCode != opcode::Opcode::OP_NONE);

			node->right->visit(this);
			byteCode.emit(opCode);

			// Special assignment operators for array/multi-dimensional arrays
			auto exprType = node->left->expressionType();
			if (exprType == ExpressionType::EXPR_ARRAY)
				opCode = opcode::Opcode::OP_ARRAY_ASSIGN;
			else if (exprType == ExpressionType::EXPR_MULTIARRAY)
				opCode = opcode::Opcode::OP_ARRAY_MULTIDIM_ASSIGN;
			else
				opCode = opcode::OP_ASSIGN;

			byteCode.emit(opCode);

			handled = true;
			break;
		}

		case ExpressionOp::ConcatAssign:
		{
			node->left->visit(this);
			node->left->visit(this);
			if (node->left->expressionType() != ExpressionType::EXPR_STRING)
				byteCode.emit(opcode::OP_CONV_TO_STRING);
			
			auto opCode = getExpressionOpCode(node->op);
			assert(opCode != opcode::Opcode::OP_JOIN);

			node->right->visit(this);
			byteCode.emit(opCode);

			// Special assignment operators for array/multi-dimensional arrays
			auto exprType = node->left->expressionType();
			if (exprType == ExpressionType::EXPR_ARRAY)
				opCode = opcode::Opcode::OP_ARRAY_ASSIGN;
			else if (exprType == ExpressionType::EXPR_MULTIARRAY)
				opCode = opcode::Opcode::OP_ARRAY_MULTIDIM_ASSIGN;
			else
				opCode = opcode::OP_ASSIGN;

			byteCode.emit(opCode);

			handled = true;
			break;
		}

		case ExpressionOp::Assign:
		{
			node->left->visit(this);

			// if the parent, and the next node are both assignments we need to
			// copy the value on the top of the stack before the next assignment op
			{
				if (copyAssignment)
				{
					byteCode.emit(opcode::OP_COPY_LAST_OP);
					copyAssignment = false;
				}

				if (node->right->isAssignment)
					copyAssignment = true;
			}

			opcode::Opcode opCode;
			pushLogicalBreakpoint(LogicalBreakPoint{});
			{
				if (node->op != ExpressionOp::Assign)
				{
					opCode = opcode::Opcode::OP_ASSIGN;
					byteCode.emit(getExpressionOpCode(node->op));
				}
				else
				{
					opCode = getExpressionOpCode(node->op);
					assert(opCode != opcode::Opcode::OP_NONE);
				}

				node->right->visit(this);

				LogicalBreakPoint& bp = logicalBreakpoints.top();
				if (!bp.breakPointLocs.empty() || !bp.continuePointLocs.empty())
				{
					bp.opbreak = bp.opcontinue = byteCode.getOpIndex();
					byteCode.emit(opcode::Opcode::OP_INLINE_CONDITIONAL);
				}
			}
			popLogicalBreakpoint();

			// Special assignment operators for array/multi-dimensional arrays
			auto exprType = node->left->expressionType();
			if (exprType == ExpressionType::EXPR_ARRAY)
				opCode = opcode::Opcode::OP_ARRAY_ASSIGN;
			else if (exprType == ExpressionType::EXPR_MULTIARRAY)
				opCode = opcode::Opcode::OP_ARRAY_MULTIDIM_ASSIGN;
			
			byteCode.emit(opCode);
			handled = true;
			break;
		}

		default:
			handled = false;
			break;
	}

	////////
	if (!handled)
	{
		printf("Undefined opcode: %s (%d)\n", ExpressionOpToString(node->op), node->op);
		Visit((Node*)node);
	}
}

void GS2CompilerVisitor::Visit(ExpressionUnaryOpNode* node)
{
	bool handled = false;

	node->expr->visit(this);

	if (node->opFirst)
	{
		switch (node->op)
		{
			case ExpressionOp::Increment:
			case ExpressionOp::Decrement:
			{
				auto opCode = getExpressionOpCode(node->op);
				assert(opCode != opcode::Opcode::OP_NONE);

				byteCode.emit(opCode);
				if (node->opUnused)
					byteCode.emit(opcode::OP_INDEX_DEC);

				handled = true;
				break;
			}

			case ExpressionOp::UnaryMinus:
			case ExpressionOp::UnaryNot:
			{
				auto opCode = getExpressionOpCode(node->op);
				assert(opCode != opcode::Opcode::OP_NONE);

				byteCode.emit(opcode::OP_CONV_TO_FLOAT);
				byteCode.emit(opCode);
				handled = true;
				break;
			}

			case ExpressionOp::UnaryStringCast:
			{
				byteCode.emit(opcode::OP_CONV_TO_STRING);

				// need to test to see if this should always be emitted here, or in postfixnode
				if (node->expr->expressionType() == ExpressionType::EXPR_ARRAY)
					byteCode.emit(opcode::OP_MEMBER_ACCESS);

				handled = true;
				break;
			}

			default:
				handled = false;
				break;
		}
	}
	else
	{
		switch (node->op)
		{
			case ExpressionOp::Increment:
			case ExpressionOp::Decrement:
			{
				auto opCode = getExpressionOpCode(node->op);
				assert(opCode != opcode::Opcode::OP_NONE);

				// TODO(joey): need to fix
				byteCode.emit(opcode::OP_COPY_LAST_OP);
				byteCode.emit(opcode::OP_CONV_TO_FLOAT);
				byteCode.emit(opcode::OP_SWAP_LAST_OPS);
				byteCode.emit(opCode);
				byteCode.emit(opcode::OP_INDEX_DEC);
				handled = true;
				break;
			}

			default:
				handled = false;
				break;
		}
	}

	if (!handled)
	{
		Visit((Node*)node);
	}
}

void GS2CompilerVisitor::Visit(ExpressionStrConcatNode *node)
{
	node->left->visit(this);
	if (node->left->expressionType() != ExpressionType::EXPR_STRING)
		byteCode.emit(opcode::OP_CONV_TO_STRING);

	switch (node->sep)
	{
		case ' ':
		case '\t':
		case '\n':
			auto id = byteCode.getStringConst(std::string(1, node->sep));
			byteCode.emit(opcode::OP_TYPE_STRING);
			byteCode.emitDynamicNumber(id);

			byteCode.emit(opcode::OP_JOIN);
			break;
	}
	
	node->right->visit(this);
	if (node->right->expressionType() != ExpressionType::EXPR_STRING)
		byteCode.emit(opcode::OP_CONV_TO_STRING);

	byteCode.emit(opcode::OP_JOIN);
}

void GS2CompilerVisitor::Visit(ExpressionCastNode* node)
{
	node->expr->visit(this);

	switch (node->type)
	{
		case ExpressionCastNode::CastType::INTEGER:
			byteCode.emit(opcode::OP_INT);
			break;

		case ExpressionCastNode::CastType::FLOAT:
			byteCode.emit(opcode::OP_CONV_TO_FLOAT);
			break;

		case ExpressionCastNode::CastType::STRING:
			byteCode.emit(opcode::OP_CONV_TO_STRING);
			break;
	}
}

void GS2CompilerVisitor::Visit(ExpressionArrayIndexNode *node)
{
	for (const auto& expr : node->exprList)
	{
		expr->visit(this);
		if (expr->expressionType() != ExpressionType::EXPR_NUMBER
			&& expr->expressionType() != ExpressionType::EXPR_INTEGER)
		{
			byteCode.emit(opcode::OP_CONV_TO_FLOAT);
		}
	}

	if (!node->isAssignment)
	{
		if (node->expressionType() == ExpressionType::EXPR_MULTIARRAY)
			byteCode.emit(opcode::OP_ARRAY_MULTIDIM);
		else
			byteCode.emit(opcode::OP_ARRAY);
	}
}

void GS2CompilerVisitor::Visit(ExpressionInOpNode *node)
{
	node->expr->visit(this);
	node->lower->visit(this);
	byteCode.emitConversionOp(node->lower->expressionType(), ExpressionType::EXPR_NUMBER);
	//if (node->lower->expressionType() != ExpressionType::EXPR_NUMBER)
	//	byteCode.emit(opcode::OP_CONV_TO_FLOAT);

	if (node->higher)
	{
		node->higher->visit(this);

		byteCode.emit(opcode::OP_IN_RANGE);
	}
	else
	{
		byteCode.emit(opcode::OP_IN_OBJ);
	}

	byteCode.emit(char(0xF3));
	byteCode.emit(char(0));
}

void GS2CompilerVisitor::Visit(ExpressionIdentifierNode *node)
{
	auto enumConstant = parserContext.getEnumConstant(node->val);
	if (enumConstant)
	{
#ifdef DBGEMITTERS
		printf("ENUM CONSTANT: %s\n", node->val.c_str());
#endif
		byteCode.emit(opcode::OP_TYPE_NUMBER);
		byteCode.emitDynamicNumber(enumConstant.value());
		return;
	}

	auto id = byteCode.getStringConst(node->val);

	byteCode.emit(opcode::OP_TYPE_VAR);
	byteCode.emit((char)0xF0);
	byteCode.emit((char)id);

#ifdef DBGEMITTERS
	printf("Identifier Node: %s\n", node->val.c_str());
#endif
}

void GS2CompilerVisitor::Visit(ExpressionIntegerNode *node)
{
	byteCode.emit(opcode::OP_TYPE_NUMBER);
	byteCode.emitDynamicNumber(node->val);
}

void GS2CompilerVisitor::Visit(ExpressionNumberNode *node)
{
	byteCode.emit(opcode::OP_TYPE_NUMBER);
	byteCode.emitDoubleNumber(node->val);
}

void GS2CompilerVisitor::Visit(ExpressionPostfixNode* node)
{
	assert(!node->nodes.empty());

	size_t i = 0;

	if (node->nodes[0]->expressionType() == ExpressionType::EXPR_IDENT)
	{
		auto identNode = reinterpret_cast<ExpressionIdentifierNode*>(node->nodes[0]);
		if (identNode->val == "this") {
			byteCode.emit(opcode::OP_THIS);
		}
		else if (identNode->val == "thiso") {
			byteCode.emit(opcode::OP_THISO);
		}
		else if (identNode->val == "player") {
			byteCode.emit(opcode::OP_PLAYER);
		}
		else if (identNode->val == "playero") {
			byteCode.emit(opcode::OP_PLAYERO);
		}
		else if (identNode->val == "level") {
			byteCode.emit(opcode::OP_LEVEL);
		}
		else if (identNode->val == "temp") {
			byteCode.emit(opcode::OP_TEMP);
		}
		else if (identNode->val == "true") {
			byteCode.emit(opcode::OP_TYPE_TRUE);
		}
		else if (identNode->val == "false") {
			byteCode.emit(opcode::OP_TYPE_FALSE);
		}
		else if (identNode->val == "null") {
			byteCode.emit(opcode::OP_TYPE_NULL);
		}
		else {
			identNode->visit(this);
			if (node->nodes.size() > 1)
				byteCode.emit(opcode::OP_CONV_TO_OBJECT);
		}
		i++;
	}
	
	// mark our last node as an assignment
	if (node->isAssignment)
		node->nodes.back()->isAssignment = true;

	for (; i < node->nodes.size(); i++)
	{
		node->nodes[i]->visit(this);

		auto exprType = node->nodes[i]->expressionType();
		if (exprType == ExpressionType::EXPR_IDENT)
		{
			byteCode.emit(opcode::OP_MEMBER_ACCESS);
			if (i != node->nodes.size() - 1)
				byteCode.emit(opcode::OP_CONV_TO_OBJECT);
		}
		else if (exprType == ExpressionType::EXPR_ARRAY)
		{

		}
	}
}

void GS2CompilerVisitor::Visit(ExpressionStringConstNode *node)
{
#ifdef DBGEMITTERS
	printf("String: %s\n", node->val.c_str());
#endif

	auto id = byteCode.getStringConst(node->val);

	byteCode.emit(opcode::OP_TYPE_STRING);
	byteCode.emitDynamicNumber(id);
}

void GS2CompilerVisitor::Visit(ExpressionFnCallNode *node)
{
	auto isObjectCall = (node->objExpr != nullptr);

	// Build-in commands
	auto& cmdList = (isObjectCall ? builtIn.builtInObjMap : builtIn.builtInCmdMap);
	std::string funcName = node->funcExpr->toString();

#ifdef DBGEMITTERS
	printf("Call Function: %s (obj call: %d)\n", funcName.c_str(), isObjectCall ? 1 : 0);
#endif

	auto iter = cmdList.find(funcName);
	BuiltInCmd cmd = (iter != cmdList.end() ? iter->second : (isObjectCall ? defaultObjCall : defaultCall));

	{
		if (cmd.useArray)
			byteCode.emit(opcode::OP_TYPE_ARRAY);

		for (auto it = node->args.rbegin(); it != node->args.rend(); ++it)
		{
			ExpressionNode *arg = *it;
			if (arg)
				arg->visit(this);
		}

		if (isObjectCall)
			node->objExpr->visit(this);

		if (cmd.convert_op != opcode::Opcode::OP_NONE)
		{
			if (byteCode.getLastOp() != cmd.convert_op && byteCode.getLastOp() != opcode::OP_THISO)
				byteCode.emit(cmd.convert_op);
		}

		if (cmd.op == opcode::OP_CALL)
		{
			node->funcExpr->visit(this);

			if (isObjectCall)
				byteCode.emit(opcode::OP_MEMBER_ACCESS);
		}

		byteCode.emit(cmd.op);
	}

	if (node->discardReturnValue)
		byteCode.emit(opcode::OP_INDEX_DEC);
}

void GS2CompilerVisitor::Visit(StatementReturnNode *node)
{
	if (node->expr)
		node->expr->visit(this);
	else
	{
		byteCode.emit(opcode::OP_TYPE_NUMBER);
		byteCode.emitDynamicNumber(0);
	}

	byteCode.emit(opcode::OP_RET);
}

void GS2CompilerVisitor::Visit(StatementIfNode* node)
{
	pushLogicalBreakpoint(LogicalBreakPoint{ });
	{
		node->expr->visit(this);
		//if (node->expr->expressionType() != ExpressionType::EXPR_INTEGER)
		//	byteCode.emit(opcode::OP_CONV_TO_FLOAT);

		// set the break point to the start of the OP_IF instruction
		logicalBreakpoints.top().opbreak = byteCode.getOpIndex();

		byteCode.emit(opcode::OP_IF);
		byteCode.emit(char(0xF4));
		byteCode.emit(short(0));
		addLogicalContinueLocation(byteCode.getBytecodePos() - 2);

		node->thenBlock->visit(this);

		// OP_IF jumps to this location if the condition is false, so we
		// continue to the next instruction, but if their is an else-block we must
		// skip the next instruction since its a jmp to the end of the if-else chain
		auto nextOpcode = byteCode.getOpIndex() + (node->elseBlock ? 1 : 0);
		logicalBreakpoints.top().opcontinue = nextOpcode;
	}
	popLogicalBreakpoint();

	if (node->elseBlock)
	{
		// emit a jump to the end of this else block for the previous if-block
		byteCode.emit(opcode::OP_SET_INDEX);
		byteCode.emit(char(0xF4));
		byteCode.emit(short(0));

		auto elseLoc = byteCode.getBytecodePos() - 2;

		node->elseBlock->visit(this);
		byteCode.emit(short(byteCode.getOpIndex()), elseLoc);
	}
}

void GS2CompilerVisitor::Visit(ExpressionNewArrayNode *node)
{
	assert(!node->dimensions.empty());

	byteCode.emit(opcode::OP_TYPE_NUMBER);
	byteCode.emitDynamicNumber(node->dimensions[0]);
	byteCode.emit(opcode::OP_ARRAY_NEW);

	for (auto i = 1; i < node->dimensions.size(); i++)
	{
		byteCode.emit(opcode::OP_TYPE_NUMBER);
		byteCode.emitDynamicNumber(node->dimensions[i]);
		byteCode.emit(opcode::OP_ARRAY_NEW_MULTIDIM);
	}
}

void GS2CompilerVisitor::Visit(ExpressionNewObjectNode *node)
{
	// TODO(joey): more testing needed
	// temp.a = new TStaticVar("str") will return a regular string,
	// but if there is additional args it has no effect on the output.
	
	for (const auto& n : node->args)
		n->visit(this);

	// TODO(joey): fix

	auto identNode = reinterpret_cast<ExpressionIdentifierNode*>(node->newExpr);
	auto id = byteCode.getStringConst(identNode->val);

	if (identNode->val == "TStaticVar") {
		byteCode.emit(opcode::OP_TYPE_VAR);
		byteCode.emitDynamicNumber(byteCode.getStringConst("unknown_object"));
	}
	else
	{
		byteCode.emit(opcode::OP_INLINE_NEW);
	}

	byteCode.emit(opcode::OP_TYPE_STRING);
	byteCode.emitDynamicNumber(id);

	//node->newExpr->visit(this);

	byteCode.emit(opcode::OP_NEW_OBJECT);
}

void GS2CompilerVisitor::Visit(StatementWhileNode *node)
{
	pushLogicalBreakpoint(LogicalBreakPoint{ });
	pushLoopBreakpoint(LogicalBreakPoint {});
	{
		auto loopStart = byteCode.getOpIndex();

		node->expr->visit(this);
		if (node->expr->expressionType() != ExpressionType::EXPR_INTEGER)
			byteCode.emit(opcode::OP_CONV_TO_FLOAT);

		byteCode.emit(opcode::OP_IF);
		byteCode.emit(char(0xF4));
		byteCode.emit(short(0));
		addLoopBreakLocation(byteCode.getBytecodePos() - 2);

		// Increment loop count
		byteCode.emit(opcode::OP_CMD_CALL);

		node->block->visit(this);

		// Jump back to condition
		byteCode.emit(opcode::OP_SET_INDEX);
		byteCode.emit(char(0xF4));
		byteCode.emit(short(0));
		addLoopContinueLocation(byteCode.getBytecodePos() - 2);
		
		// Set the breakpoint to after the while-statement
		loopBreakpoints.top().opbreak = byteCode.getOpIndex();
		loopBreakpoints.top().opcontinue = loopStart;
	}
	popLoopBreakpoint();
	popLogicalBreakpoint();
}

void GS2CompilerVisitor::Visit(StatementBreakNode* node)
{
	if (loopBreakpoints.empty())
	{
#ifdef DBGEMITTERS
		printf("Error, no loops to break from.\n");
#endif
		return;
	}

	// Emit jump out of loop
	byteCode.emit(opcode::OP_SET_INDEX);
	byteCode.emit(char(0xF4));
	byteCode.emit(short(0));
	addLoopBreakLocation(byteCode.getBytecodePos() - 2);
}

void GS2CompilerVisitor::Visit(StatementContinueNode* node)
{
	if (loopBreakpoints.empty())
	{
#ifdef DBGEMITTERS
		printf("Error, no loops to continue.\n");
#endif
		return;
	}

	// Emit jump back to the loop-condition
	byteCode.emit(opcode::OP_SET_INDEX);
	byteCode.emit(char(0xF4));
	byteCode.emit(short(0));
	addLoopContinueLocation(byteCode.getBytecodePos() - 2);
}

void GS2CompilerVisitor::Visit(StatementForNode* node)
{
	// Emit init expression
	if (node->init)
		node->init->visit(this);

	// Start of loop
	auto loopStart = byteCode.getOpIndex();

	// Emit the condition expression
	if (node->cond)
	{
		node->cond->visit(this);
		byteCode.emitConversionOp(node->cond->expressionType(), ExpressionType::EXPR_NUMBER);

		//if (node->cond->expressionType() != ExpressionType::EXPR_INTEGER)
		//	byteCode.emit(opcode::OP_CONV_TO_FLOAT);
	}
	else
	{
		// No condition, so while (true)
		// note: parser should throw a syntax error so this should never really occur.
		byteCode.emit(opcode::OP_TYPE_TRUE);
	}

	pushLoopBreakpoint(LogicalBreakPoint{});
	{
		// Emit if-loop on conditional expression, with a failed jump to the end-block
		byteCode.emit(opcode::OP_IF);
		byteCode.emit(char(0xF4));
		byteCode.emit(short(0));
		addLoopBreakLocation(byteCode.getBytecodePos() - 2);
		
		// Increment loop count
		byteCode.emit(opcode::OP_CMD_CALL);

		// Emit block
		if (node->block)
			node->block->visit(this);

		// Emit post-op
		if (node->postop)
			node->postop->visit(this);

		// Emit jump back to condition
		byteCode.emit(opcode::OP_SET_INDEX);
		byteCode.emitDynamicNumber(loopStart);

		loopBreakpoints.top().opbreak = byteCode.getOpIndex();
		loopBreakpoints.top().opcontinue = loopStart;
	}
	popLoopBreakpoint();
}

//////////// not implemented yet

void GS2CompilerVisitor::Visit(StatementNewNode* node)
{
	assert(node->args.size() == 1);

	// emit args
	for (const auto& n : node->args)
		n->visit(this);

	byteCode.emit(opcode::OP_INLINE_NEW);

	byteCode.emit(opcode::OP_COPY_LAST_OP);
	byteCode.emit(opcode::OP_COPY_LAST_OP);
	byteCode.emit(opcode::OP_COPY_LAST_OP);

	// emit object type
	auto id = byteCode.getStringConst(node->ident);
	byteCode.emit(opcode::OP_TYPE_STRING);
	byteCode.emitDynamicNumber(id);

	// official emits this
	byteCode.emit(opcode::OP_CONV_TO_STRING);

	byteCode.emit(opcode::OP_NEW_OBJECT);
	byteCode.emit(opcode::OP_ASSIGN);

	// with statement
	byteCode.emit(opcode::OP_CONV_TO_OBJECT);

	byteCode.emit(opcode::OP_WITH);
	byteCode.emit(char(0xF4));
	byteCode.emit(short(0));

	auto withLoc = byteCode.getBytecodePos() - 2;

	int prevNewObjectCount = newObjectCount++;
	if (node->stmtBlock)
		node->stmtBlock->visit(this);

	byteCode.emit(opcode::OP_WITHEND);
	byteCode.emit(short(byteCode.getOpIndex()), withLoc);

	///////
	// call addcontrol
	for (int i = 0; i < newObjectCount - prevNewObjectCount; i++)
	{
		byteCode.emit(opcode::OP_TYPE_ARRAY);
		byteCode.emit(opcode::OP_SWAP_LAST_OPS);

		auto addControlId = byteCode.getStringConst("addcontrol");
		byteCode.emit(opcode::OP_TYPE_VAR);
		byteCode.emitDynamicNumber(addControlId);
		byteCode.emit(opcode::OP_CALL);
		byteCode.emit(opcode::OP_INDEX_DEC);
	}

	newObjectCount--;
}

void GS2CompilerVisitor::Visit(StatementWithNode* node)
{
	node->expr->visit(this);
	byteCode.emit(opcode::OP_CONV_TO_OBJECT);

	byteCode.emit(opcode::OP_WITH);
	byteCode.emit(char(0xF4));
	byteCode.emit(short(0));

	auto withLoc = byteCode.getBytecodePos() - 2;
	if (node->block)
		node->block->visit(this);

	byteCode.emit(opcode::OP_WITHEND);
	byteCode.emit(short(byteCode.getOpIndex()), withLoc);
}

void GS2CompilerVisitor::Visit(ExpressionListNode* node)
{
	byteCode.emit(opcode::OP_TYPE_ARRAY);

	for (auto it = node->args.rbegin(); it != node->args.rend(); ++it)
	{
		assert(*it);
		(*it)->visit(this);
	}

	byteCode.emit(opcode::OP_ARRAY_END);
}

void GS2CompilerVisitor::Visit(StatementForEachNode *node)
{
	// push name / expression
	node->name->visit(this);
	node->expr->visit(this);
	byteCode.emit(opcode::OP_CONV_TO_OBJECT);

	// push index to stack
	byteCode.emit(opcode::OP_TYPE_NUMBER);
	byteCode.emitDynamicNumber(0);

	pushLoopBreakpoint(LogicalBreakPoint {});
	{
		auto startLoopOp = byteCode.getOpIndex();
		byteCode.emit(opcode::OP_FOREACH);
		byteCode.emit(char(0xF4));
		byteCode.emit(short(0));

		// Add break location for the jump out of the loop
		addLoopBreakLocation(byteCode.getBytecodePos() - 2);

		byteCode.emit(opcode::OP_CMD_CALL);
		node->block->visit(this);

		// increase idx
		auto continueLoopOp = byteCode.getOpIndex();
		byteCode.emit(opcode::OP_INC);

		// jump to beginning of the for-each loop
		byteCode.emit(opcode::OP_SET_INDEX);
		byteCode.emitDynamicNumber(startLoopOp);

		// Write out the breakpoint jumps
		auto endLoopOp = byteCode.getOpIndex();
		loopBreakpoints.top().opbreak = endLoopOp;
		loopBreakpoints.top().opcontinue = continueLoopOp;
	}
	popLoopBreakpoint();

	// pop the idx variable
	byteCode.emit(opcode::OP_INDEX_DEC);
}

void GS2CompilerVisitor::Visit(StatementSwitchNode* node)
{
	// emit jump to case-test
	// case-list:
	// record case-block start
	// emit case-block
	// emit jump to endloc // actually no
	// ...repeat..

	// case-test:
	// push switch-expr
	// copy last operand
	// push case-expr
	// push ==
	// opcode 2 if equal, jmp to corresponding case-block

	// endloc:
	// ....

	pushLoopBreakpoint(LogicalBreakPoint{});
	{
		std::vector<uint32_t> caseStartOp;

		// jump to case-test
		byteCode.emit(opcode::OP_SET_INDEX);
		byteCode.emit(char(0xF4));
		byteCode.emit(short(0));

		size_t caseTestLoc = byteCode.getBytecodePos() - 2;

		// case-list:
		for (const auto& caseNode : node->cases)
		{
			for (const auto& caseExpr : caseNode.exprList)
				caseStartOp.push_back(byteCode.getOpIndex());
			caseNode.block->visit(this);
		}

		// case-test:
		byteCode.emit(short(byteCode.getOpIndex()), caseTestLoc);
		node->expr->visit(this);

		size_t i = 0;
		for (const auto& caseNode : node->cases)
		{
			for (const auto& caseExpr : caseNode.exprList)
			{
				if (caseExpr)
				{
					byteCode.emit(opcode::OP_COPY_LAST_OP);
					caseExpr->visit(this);
					byteCode.emit(opcode::OP_EQ);
					byteCode.emit(opcode::OP_SET_INDEX_TRUE);
				}
				else byteCode.emit(opcode::OP_SET_INDEX);

				byteCode.emitDynamicNumber(caseStartOp[i++]);
			}
		}

		if (node->expr->expressionType() == ExpressionType::EXPR_FUNCTION)
			byteCode.emit(opcode::OP_INDEX_DEC);

		loopBreakpoints.top().opbreak = byteCode.getOpIndex();
	}
	popLoopBreakpoint();
}

// not implemented: should never occur
void GS2CompilerVisitor::Visit(StatementNode *node) { Visit((Node *)node); }
void GS2CompilerVisitor::Visit(ExpressionNode *node) { Visit((Node *)node); }

///////////////////

void GS2CompilerVisitor::popBreakpoint(std::stack<LogicalBreakPoint>& bp)
{
	auto& breakPoint = bp.top();
	if (breakPoint.opbreak >= 0)
	{
		for (const auto& loc : breakPoint.breakPointLocs)
			byteCode.emit(short(breakPoint.opbreak), loc);
	}

	if (breakPoint.opcontinue >= 0)
	{
		for (const auto& loc : breakPoint.continuePointLocs)
			byteCode.emit(short(breakPoint.opcontinue), loc);
	}

	bp.pop();
}