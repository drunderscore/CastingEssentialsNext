#pragma once

namespace Hooking{ namespace Internal
{
	template<class Type, class RetVal, class... Args> using MemberFnPtr_Const = RetVal(Type::*)(Args...) const;
	template<class Type, class RetVal, class... Args> using MemberFnPtr = RetVal(Type::*)(Args...);
	template<class Type, class RetVal, class... Args> using MemFnVaArgsPtr = RetVal(Type::*)(Args..., ...);
	template<class Type, class RetVal, class... Args> using MemFnVaArgsPtr_Const = RetVal(Type::*)(Args..., ...) const;

	template<class Type, class RetVal, class... Args> using LocalDetourFnPtr = RetVal(*)(Type*, Args...);
	template<class RetVal, class... Args> using GlobalDetourFnPtr = RetVal(*)(Args...);
	template<class Type, class RetVal, class... Args> using LocalFnPtr = RetVal(*)(Type*, Args...);

	template<class RetVal, class... Args> using GlobalVaArgsFnPtr = RetVal(*)(Args..., ...);
	template<class Type, class RetVal, class... Args> using LocalVaArgsFnPtr = RetVal(*)(Type*, Args..., ...);

	template<class RetVal, class... Args> using GlobalFunctionalType = std::function<RetVal(Args...)>;
	template<class Type, class RetVal, class... Args> using LocalFunctionalType = std::function<RetVal(Type*, Args...)>;
} }