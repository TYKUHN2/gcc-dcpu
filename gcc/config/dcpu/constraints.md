;
;	Target definition for the DCPU-16 specifically the TechCompliant variety
;	Copyright (C) 2019 Tyler Kuhn
;	
;	This file is an addition to GCC, not part of GCC.
;	
;	The purpose of this file is to define the opcodes available for the DCPU and how they operate
;

(define_register_constraint "a" "A" "The A register")
(define_register_constraint "b" "B" "The B register")
(define_register_constraint "c" "C" "The C register")

(define_register_constraint "x" "X" "The X register")
(define_register_constraint "y" "Y" "The Y register")
(define_register_constraint "z" "Z" "The Z register")

(define_register_constraint "j" "IJNC" "The I and J registers")

(define_register_constraint "e" "EX" "The EX register")