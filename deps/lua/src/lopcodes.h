/*
** $Id: lopcodes.h,v 1.125.1.1 2007/12/27 13:02:25 roberto Exp $
** Opcodes for Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lopcodes_h
#define lopcodes_h

#include "llimits.h"

 /*
    一般来说，一个字节码指令是由1字节的操作码(opcode)和若干个可选操作
    参数组成，

    Lua虚拟机中的寄存器实际上用的是栈上的空间，因此A域的值指代的就是
    栈上的某个位置，在这个指令里被当做寄存器使用了。
    A域栈8bit,它能代表的最大值是256，实际上限制了栈的有效空间

    接着就是B域和C域，这两个域主要存放参数信息。
    它们各占9bit,能表示的最大范围是2的9次方，也就是512，
    B域和C域具体代表什么意思实际上是由opcode决定的。

    比如操作对象如果是字符串，字符串本身的内容是没法编入这么短的域中的；
    因此需要将不同的信息存放到不同的位置，再将他们的索引信息找出编入指令中。
    Lua的绝大多数指令，采用的是iABC模式。

    iABx模式 没有B和C域，而只有一个Bx域，这个域占18bit.使用这种模式的指令很少，
    只有OP_LOADK,OP_LOADKX和OP_CLOSURE三各指令。Bx所代表的值是无符号的。

    iAsBx的模式和iABx⼤体相当，只是⾼18bit的值是有符号
的，可以表示负数。但是sBx域并没有采取⼆进制补码的⽅式，⽽是采⽤⼀个bias值作为0值分
界。因为sBx域占18bit，因此它能表示的最⼤⽆符号整数值是262143（218-1）。那么它的零
值则是131071（262143>>1）。当需要它表示-1时，那么sBx域的值为131070（131071
1）。采⽤iAsBx模式的指令⼀般和跳转有关系，⽐如OP_JMP、OP_FORLOOP、
OP_FORPREP和OP_TFORLOOP指令等。
 */

/*

 字节码指令格式和操作码定义。容易实现
  R(A)
  Register A (specified in instruction field A)
  R(B)
  Register B (specified in instruction field B)
  R(C)
  Register C (specified in instruction field C)
  PC
  Program Counter
  Kst(n)
  Element n in the constant list
  Upvalue[n]
  Name of upvalue with index n
  Gbl[sym]
  Global variable indexed by symbol sym
  RK(B)
  Register B or a constant index
  RK(C)
  Register C or a constant index
  sBx
  Signed displacement (in field sBx) for all kinds of jumps

  Lua bytecode instructions are 32-bits in size. 
  All instructions have an opcode in the first 6 bits. Instructions can have the following fields:
  Lua字节码指令的大小为32位。所有指令的前6位都有一个操作码。指令可以有以下字段

  'A' : 8 bits
  'B' : 9 bits
  'C' : 9 bits
  'Ax' : 26 bits ('A', 'B', and 'C' together)
  'Bx' : 18 bits ('B' and 'C' together)
  'sBx' : signed Bx

  A signed argument is represented in excess K; that is, the number value is the unsigned value minus K. K is exactly the maximum value for that argument (so that -max is represented by 0, and +max is represented by 2*max), 
  which is half the maximum for the corresponding unsigned argument.
*/

/*===========================================================================
  We assume that instructions are unsigned numbers.
  All instructions have an opcode in the first 6 bits.
  Instructions can have the following fields:
	`A' : 8 bits
	`B' : 9 bits
	`C' : 9 bits
	`Bx' : 18 bits (`B' and `C' together)
	`sBx' : signed Bx

  A signed argument is represented in excess K; that is, the number
  value is the unsigned value minus K. K is exactly the maximum value
  for that argument (so that -max is represented by 0, and +max is
  represented by 2*max), which is half the maximum for the corresponding
  unsigned argument.
===========================================================================*/


enum OpMode {iABC, iABx, iAsBx};  /*三种指令格式 basic instruction format */


/*
** size and position of opcode arguments.

   定义了在一个指令中每个参数的对应大小和位置
*/
#define SIZE_C		9
#define SIZE_B		9
#define SIZE_Bx		(SIZE_C + SIZE_B) //18
#define SIZE_A		8

#define SIZE_OP		6

#define POS_OP		0
#define POS_A		(POS_OP + SIZE_OP)
#define POS_C		(POS_A + SIZE_A)
#define POS_B		(POS_C + SIZE_C)
#define POS_Bx		POS_C


/*
** limits for opcode arguments.
** we use (signed) int to manipulate most arguments,
** so they must fit in LUAI_BITSINT-1 bits (-1 for sign)
*/
#if SIZE_Bx < LUAI_BITSINT-1
#define MAXARG_Bx        ((1<<SIZE_Bx)-1)
#define MAXARG_sBx        (MAXARG_Bx>>1)         /* `sBx' is signed */
#else
#define MAXARG_Bx        MAX_INT
#define MAXARG_sBx        MAX_INT
#endif


#define MAXARG_A        ((1<<SIZE_A)-1)
#define MAXARG_B        ((1<<SIZE_B)-1)
#define MAXARG_C        ((1<<SIZE_C)-1)


/* creates a mask with `n' 1 bits at position `p' */
#define MASK1(n,p)	((~((~(Instruction)0)<<n))<<p)

/* creates a mask with `n' 0 bits at position `p' */
#define MASK0(n,p)	(~MASK1(n,p))

/*
** the following macros help to manipulate instructions
*/

#define GET_OPCODE(i)	(cast(OpCode, ((i)>>POS_OP) & MASK1(SIZE_OP,0)))
#define SET_OPCODE(i,o)	((i) = (((i)&MASK0(SIZE_OP,POS_OP)) | \
		((cast(Instruction, o)<<POS_OP)&MASK1(SIZE_OP,POS_OP))))

#define GETARG_A(i)	(cast(int, ((i)>>POS_A) & MASK1(SIZE_A,0)))
#define SETARG_A(i,u)	((i) = (((i)&MASK0(SIZE_A,POS_A)) | \
		((cast(Instruction, u)<<POS_A)&MASK1(SIZE_A,POS_A))))

#define GETARG_B(i)	(cast(int, ((i)>>POS_B) & MASK1(SIZE_B,0)))
#define SETARG_B(i,b)	((i) = (((i)&MASK0(SIZE_B,POS_B)) | \
		((cast(Instruction, b)<<POS_B)&MASK1(SIZE_B,POS_B))))

#define GETARG_C(i)	(cast(int, ((i)>>POS_C) & MASK1(SIZE_C,0)))
#define SETARG_C(i,b)	((i) = (((i)&MASK0(SIZE_C,POS_C)) | \
		((cast(Instruction, b)<<POS_C)&MASK1(SIZE_C,POS_C))))

#define GETARG_Bx(i)	(cast(int, ((i)>>POS_Bx) & MASK1(SIZE_Bx,0)))
#define SETARG_Bx(i,b)	((i) = (((i)&MASK0(SIZE_Bx,POS_Bx)) | \
		((cast(Instruction, b)<<POS_Bx)&MASK1(SIZE_Bx,POS_Bx))))

#define GETARG_sBx(i)	(GETARG_Bx(i)-MAXARG_sBx)
#define SETARG_sBx(i,b)	SETARG_Bx((i),cast(unsigned int, (b)+MAXARG_sBx))


#define CREATE_ABC(o,a,b,c)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, b)<<POS_B) \
			| (cast(Instruction, c)<<POS_C))

#define CREATE_ABx(o,a,bc)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, bc)<<POS_Bx))


/*
** Macros to operate RK indices
*/

/* this bit 1 means constant (0 means register) */
#define BITRK		(1 << (SIZE_B - 1))

// 判断这个数据的第8位是不是1，是则认为应该从K数组中获取数据，否则就从函数栈寄存器中获取数据。
/* test whether value is a constant */
#define ISK(x)		((x) & BITRK)

/* gets the index of the constant */
#define INDEXK(r)	((int)(r) & ~BITRK)

#define MAXINDEXRK	(BITRK - 1)

/* code a constant index as a RK value */
#define RKASK(x)	((x) | BITRK)


/*
** invalid register that fits in 8 bits
*/
#define NO_REG		MAXARG_A


/*
** R(x) - register
** Kst(x) - constant (in constant table)
** RK(x) == if ISK(x) then Kst(INDEXK(x)) else R(x)
*/


/*
** grep "ORDER OP" if you change these enums
   如果更改这些枚举，则执行“ORDER OP”

   OP_MOVE 从R(B)中取数据赋值给R(A)
   OP_LOADK 从Kst(Bx)常量中 取数据赋值给R(A) //比如local name = "zhangsan"
   OP_LOADBOOL 取B参数的布尔值赋值给R(A),如果满足C为真的条件，则将pc指针递增，即执行下一条指令

   OP_LOADNIL  从寄存器R(A) 到R(B) 的数据赋值为nil
   OP_GETUPVAL 从UpValue数组中取值赋值给R(A)
   OP_GETGLOBAL 以Kst[Bx] 作为全局符号表的索引，取出值后赋值给R(A)
   OP_GETTABLE 以RK(C)作为表索引，以R(B)的数据作为表，取出来的数据赋值给R(A)

   OP_SETGLOBAL 将R(A)的值赋值给以Kst[Bx] 作为全局符号表的索引的全局变量
   OP_SETUPVAL  将R(A)的值赋值给以B作为upvalue数组索引的变量
   OP_SETTABLE  将RK(C)的值赋值给R(A) 表中索引为RK(B)的变量
   OP_NEWTABLE  创建一个新的表，并将其赋值给R(A),其中数组部分的初始大小是B,散列部分的大小是C
   OP_SELF  做好调用成员函数之前的准备，其中待调用模块赋值到R(A+1)中，
            而待调用的成员函数存放在R(A)中，待调用的模块存放在R(B)中，待调用的函数名放在RK(C)中


   OP_ADD  加法操作
   OP_SUB  减法操作
   OP_MUL  乘法操作
   OP_DIV  除法操作
   OP_MOD  模操作
   OP_POW  乘方操作
   OP_UNM  取负操作
   OP_NOT  非操作

   OP_LEN  取长度操作
   OP_CONCAT 连接操作
   OP_JMP  跳转操作
   OP_EQ   比较相等操作，如果比较RK(B)和RK(C) 所得的结果不等于A,那么递增pc指令
   OP_LT  比较小于操作，如果比较RK(B) 小于RK(C)所得的结果不等于A,那么递增pc指令
   OP_LE  比较小于等于操作，如果比较RK(B) 小于等于RK(C)所得的结果不等于A,那么递增pc指令

   OP_TEST  测试操作，如果R(A)参数的布尔值不等于C,将pc指针加1，直接跳过下一条指令的执行
   OP_TESTSET 测试设置操作，与OP_TEST指令类似，所不同的是当比较的参数不相等时，执行一个赋值操作
   OP_CALL  调用函数指令，其中函数地址存放在R(A),函数参数数量存放在B中，有两种情况：
            1）为0表示参数从A+1的位置一直到函数栈的top位置，这表示函数参数中有另外的函数调用，
               因为在调用时并不知道有多少参数，所以只好告诉虚拟机函数参数一直到函数栈的top为止了
            2）大于0时函数参数数量为B-1

   OP_TAILCALL  尾调用操作，R(A) 存放函数地址，参数B表示函数参数数量，意义与前面OP_CALL指令的
                B参数一样，C参数在这里恒为0 表示有多个返回值。

   OP_RETURN    返回操作，R(A)表示函数参数的起始地址，B参数用于标示函数参数数量，有两种情况：
                1）为0表示参数从A+1的位置一直到函数栈的top位置，这表示函数参数中有另外的函数
                   调用，因为在调用时并不知道有多少参数，所以只好告诉虚拟机函数参数一直到
                   函数栈的top位置了；
                2）大于0时函数参数数量为B-1。参数C表示函数返回值数量，也有两种情况：
                    1）为0时表示有可变数量的值返回。2）为1时表示返回值数量为C-1

  OP_FORLOOP  数字for的循环操作，根据循环步长来更新循环变量，判断循环条件是否终止
              如果没有，就跳转到循环体继续执行下一次循环，否则退出循环。R(A)存放
              循环变量的初始值，R(A+1) 存放循环终止值，R(A+2）存放循环步长值，
               R(A+3) 存放循环变量，sBx参数存放循环体开始指令的偏移量

  OP_FORPREP  数字for循环准备操作。R(A)存放循环变量的初始值，R(A+1) 存放
              循环终止值，R(A+2)存放循环步长值，R(A+3) 存放循环变量，
              sBx参数存放紧跟着的OP_FORLOOP指令的偏移量

  OP_TFORLOOP   泛型循环操作
  OP_SETLIST    对表的数组部分进行赋值

  OP_CLOSE      关闭所有在函数栈中位置在R(A)以上的变量
  OP_CLOSURE    创建一个函数对象，其中函数Proto信息存放在Bx中，生成的函数
                对象存放在R(A)中，这个指令后面可能会跟着MOVE或者
                GET_UPVAL指令，取决于引用到的外部参数的位置，这些外部参数
                的数量由n决定。
  OP_VARARG     可变参数赋值操作



   R(A) A参数作为寄存器索引，
   pc 程序计数器(program counter),这个数据用于指示当前指令的地址
   Kst(n) 常量数组中的第n个数据
   Upvalue(n) upvalue数组中的第n个数据
   Gbl[sym] 全局符号表中取名为sym的数据
   RK(B) B可能是寄存器索引，也可能是常量数组索引，
   sBx 有符号整数，用于标示跳转偏移量。
*/

typedef enum {
/*----------------------------------------------------------------------
name		args	description
------------------------------------------------------------------------*/
OP_MOVE,/*	A B	R(A) := R(B)			Copy a value between registers		*/
OP_LOADK,/*	A Bx	R(A) := Kst(Bx)				Load a constant into a register	*/
OP_LOADBOOL,/*	A B C	R(A) := (Bool)B; if (C) pc++		Load a boolean into a register	*/
OP_LOADNIL,/*	A B	R(A) := ... := R(B) := nil		Load nil values into a range of registers	*/
OP_GETUPVAL,/*	A B	R(A) := UpValue[B]		Read an upvalue into a register		*/

OP_GETGLOBAL,/*	A Bx	R(A) := Gbl[Kst(Bx)]				*/
OP_GETTABLE,/*	A B C	R(A) := R(B)[RK(C)]				*/

OP_SETGLOBAL,/*	A Bx	Gbl[Kst(Bx)] := R(A)				*/
OP_SETUPVAL,/*	A B	UpValue[B] := R(A)				*/
OP_SETTABLE,/*	A B C	R(A)[RK(B)] := RK(C)				*/

OP_NEWTABLE,/*	A B C	R(A) := {} (size = B,C)				*/

OP_SELF,/*	A B C	R(A+1) := R(B); R(A) := R(B)[RK(C)]		*/

OP_ADD,/*	A B C	R(A) := RK(B) + RK(C)				*/
OP_SUB,/*	A B C	R(A) := RK(B) - RK(C)				*/
OP_MUL,/*	A B C	R(A) := RK(B) * RK(C)				*/
OP_DIV,/*	A B C	R(A) := RK(B) / RK(C)				*/
OP_MOD,/*	A B C	R(A) := RK(B) % RK(C)				*/
OP_POW,/*	A B C	R(A) := RK(B) ^ RK(C)				*/
OP_UNM,/*	A B	R(A) := -R(B)					*/
OP_NOT,/*	A B	R(A) := not R(B)				*/
OP_LEN,/*	A B	R(A) := length of R(B)				*/

OP_CONCAT,/*	A B C	R(A) := R(B).. ... ..R(C)			*/

OP_JMP,/*	sBx	pc+=sBx					*/

OP_EQ,/*	A B C	if ((RK(B) == RK(C)) ~= A) then pc++		*/
OP_LT,/*	A B C	if ((RK(B) <  RK(C)) ~= A) then pc++  		*/
OP_LE,/*	A B C	if ((RK(B) <= RK(C)) ~= A) then pc++  		*/

OP_TEST,/*	A C	if not (R(A) <=> C) then pc++			*/ 
OP_TESTSET,/*	A B C	if (R(B) <=> C) then R(A) := R(B) else pc++	*/ 

OP_CALL,/*	A B C	R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) */
OP_TAILCALL,/*	A B C	return R(A)(R(A+1), ... ,R(A+B-1))		*/
OP_RETURN,/*	A B	return R(A), ... ,R(A+B-2)	(see note)	*/

OP_FORLOOP,/*	A sBx	R(A)+=R(A+2);
			if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) }*/
OP_FORPREP,/*	A sBx	R(A)-=R(A+2); pc+=sBx				*/

OP_TFORLOOP,/*	A C	R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2)); 
                        if R(A+3) ~= nil then R(A+2)=R(A+3) else pc++	*/ 
OP_SETLIST,/*	A B C	R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B	*/

OP_CLOSE,/*	A 	close all variables in the stack up to (>=) R(A)*/
OP_CLOSURE,/*	A Bx	R(A) := closure(KPROTO[Bx], R(A), ... ,R(A+n))	*/

OP_VARARG/*	A B	R(A), R(A+1), ..., R(A+B-1) = vararg		可变参数赋值操作 */
} OpCode;


#define NUM_OPCODES	(cast(int, OP_VARARG) + 1)



/*===========================================================================
  Notes:
  (*) In OP_CALL, if (B == 0) then B = top. C is the number of returns - 1,
      and can be 0: OP_CALL then sets `top' to last_result+1, so
      next open instruction (OP_CALL, OP_RETURN, OP_SETLIST) may use `top'.

  (*) In OP_VARARG, if (B == 0) then use actual number of varargs and
      set top (like in OP_CALL with C == 0).

  (*) In OP_RETURN, if (B == 0) then return up to `top'

  (*) In OP_SETLIST, if (B == 0) then B = `top';
      if (C == 0) then next `instruction' is real C

  (*) For comparisons, A specifies what condition the test should accept
      (true or false).

  (*) All `skips' (pc++) assume that next instruction is a jump
===========================================================================*/


/*
** masks for instruction properties. The format is:
** bits 0-1: op mode
** bits 2-3: C arg mode
** bits 4-5: B arg mode
** bit 6: instruction set register A
** bit 7: operator is a test
*/  

enum OpArgMask {
  OpArgN,  /* argument is not used 参数未被使用  只是没有作为R()或RK()宏的参数使用 */
  OpArgU,  /* argument is used 已使用参数 */
  OpArgR,  /* 表示该参数是寄存器或跳转偏移 argument is a register or a jump offset */
  OpArgK   /* 表示该参数是常量还是寄存器，K表示常量 argument is a constant or register/constant */
};

LUAI_DATA const lu_byte luaP_opmodes[NUM_OPCODES];

#define getOpMode(m)	(cast(enum OpMode, luaP_opmodes[m] & 3))
#define getBMode(m)	(cast(enum OpArgMask, (luaP_opmodes[m] >> 4) & 3))
#define getCMode(m)	(cast(enum OpArgMask, (luaP_opmodes[m] >> 2) & 3))
#define testAMode(m)	(luaP_opmodes[m] & (1 << 6))
#define testTMode(m)	(luaP_opmodes[m] & (1 << 7))


LUAI_DATA const char *const luaP_opnames[NUM_OPCODES+1];  /* opcode names */


/* number of list items to accumulate before a SETLIST instruction */
#define LFIELDS_PER_FLUSH	50


#endif
