;
;	Target definition for the DCPU-16 specifically the TechCompliant variety
;	Copyright (C) 2019 Tyler Kuhn
;	
;	This file is an addition to GCC, not part of GCC.
;	
;	The purpose of this file is to define the opcodes available for the DCPU and how they operate
;

(define_register_constraint "a" "A_REG" "The A register")
(define_register_constraint "b" "B_REG" "The B register")
(define_register_constraint "c" "C_REG" "The C register")

(define_register_constraint "x" "X_REG" "The X register")
(define_register_constraint "y" "Y_REG" "The Y register")
(define_register_constraint "z" "Z_REG" "The Z register")

(define_register_constraint "j" "IJNC_REG" "The I and J registers")
(define_register_constraint "e" "EX_REG" "The EX register")