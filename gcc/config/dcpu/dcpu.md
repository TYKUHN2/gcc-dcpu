;
;	Target definition for the DCPU-16 specifically the TechCompliant variety
;	Copyright (C) 2019 Tyler Kuhn
;	
;	This file is an addition to GCC, not part of GCC.
;	
;	The purpose of this file is to define the opcodes available for the DCPU and how they operate
;

; INSTRUCTIONS REFERENCE FOR DEVELOPMENT ONLY
;
;	SINGLE INSTRUCTIONS
;
;	UND
;	JSR
;	UND
;	UND
;	UND
;	UND
;	UND
;	UND
;	INT
;	IAG
;	IAS
;	RFI
;	IAQ
;	UND
;	UND
;	UND
;	HWN
;	HWQ
;	HWI
;	LOG
;	BRK
;	HLT	NO OPERAND
;	UND
;	UND
;	UND
;	UND
;	UND
;	UND
;	UND
;	UND
;	UND
;	UND
;
;	DOUBLE INSTRUCTIONS
;
;	BAD	SEE SINGLE INSTRUCTIONS
;	SET
;	ADD
;	SUB
;	MUL
;	MLI
;	DIV
;	DVI
;	MOD
;	MDI
;	AND
;	BOR
;	XOR
;	SHR
;	ASR
;	SHL
;	IFB
;	IFC
;	IFE
;	IFN
;	IFG
;	IFA
;	IFL
;	IFU
;	UND
;	UND
;	ADX
;	SBX
;	UND
;	UND
;	STI
;	STD

(include "constraints.md")

;
;	Define useful macros
;
(define_constants [
	(REG_I 6)
	(REG_J 7)
])

;
;	Define certain built-in GCC operations
;

; NOP Definition
(define_insn "nop"
	[(const_int 0)]
	""
	"SET A, A"
)

; CALL Definition
(define_insn "call"
	[(call	(match_operand:HI 0 "memory_operand")
			(match_operand:HI 1 "")
	)]
	""
	"JSR %0"
)

; JUMP Definitions
(define_insn "jump"
	[(set	(pc)
			(label_ref (match_operand 0 ""))
	)]
	""
	"SET PC, %0"
)

(define_insn "indirect_jump"
	[(set	(pc)
			(match_operand:HI 0 "")
	)]
	""
	"SET PC, %0"
)

; CONDITIONAL Definitions
(define_expand "cbranchhi4"
	[(cond_exec
		(match_operator 0 "comparison_operator" [
			(match_operand:HI 1 "")
			(match_operand:HI 2 "")
		])
		(set (pc) (label_ref (match_operand 3 "")))
	)]
	""
	""
)

;
;	Define instructions not previously defined
;

; Define the various IFs the DCPU has
(define_code_iterator condop [ne eq lt ltu gt gtu])
(define_code_attr cmp [
	(ne "IFN")
	(eq "IFE")
	(lt "IFL")
	(ltu "IFU")
	(gt "IFG")
	(gtu "IFA")
])

(define_insn "*<cmp>"
	[(condop 	(match_operand:HI 0 "")
				(match_operand:HI 1 "")
	)]
	""
	"<cmp> $1, $0"
)

; Define some IFs the DCPU does not have
(define_code_iterator condop2 [ge geu le leu])
(define_code_attr pcmp [
	(ge "IFG")
	(geu "IFA")
	(le "IFL")
	(leu "IFU")
])

(define_insn "*<pcmp>ORE"
	[(condop2	(match_operand:HI 0 "")
				(match_operand:HI 1 "")
	)]
""
"<pcmp> $1, $0
IFE $1, $0"
)

; Define the iteration instructions
(define_insn "*sti"
	[(set 	(match_operand:HI 0 "")
			(match_operand:HI 1 ""))
	(set	(reg REG_I)
			(plus (reg REG_I) (const_int 1)))
	(set	(reg REG_J)
			(plus (reg REG_J) (const_int 1))
	)]
	"true"
	"STI $1, $0"
)

(define_insn "*std"
	[(set 	(match_operand:HI 0 "")
			(match_operand:HI 1 ""))
	(set	(reg REG_I)
			(minus (reg REG_I) (const_int 1)))
	(set	(reg REG_J)
			(minus (reg REG_J) (const_int 1))
	)]
	""
	"STD $1, $0"
)
