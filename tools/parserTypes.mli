(** These are the types that are mapped-to during the parsing of flux programs *)
type position = { file: string option
		; lineno: int 
		; characterno: int }

val noposition : position

val position_to_string : position -> string

type 'a positioned = 'a * position * position

type mainfun =  { sourcename: string positioned
		; sourcefunction : string 
		; successor: string positioned option 
                ; runonce: bool
                }

type data_type = { dctypemod: string positioned
		; dctype: string positioned }

type decl_formal = 
		{ ctypemod: string positioned 
		; ctype: string positioned 
		; name: string positioned
		}
type guardmod = Read | Write

type guardref = { guardname: string positioned 
		; arguments: string positioned list
		; modifiers: guardmod list
		; localgname: string positioned option
		}

type general_formal = 
	Typed of decl_formal 
	| GuardRef of guardref

type node_decl = 
	{ detached: bool
	; abstract: bool
	; nodename: string positioned 
	; nodefunction: string
	; inputs: decl_formal list 
	; guardrefs: guardref list
	; outputs: decl_formal list option
	}

type cond_decl =
	{ condname: string positioned
	; condfunction: string
	; condinputs: decl_formal list
	}

type atom_decl =
	{ atomname: string positioned
	; atominputs: decl_formal list
	; outputtype: data_type
	; atomtype: string
	}

type ident = string positioned 

type comma_item = Star | Ident of string positioned

type expr_type = Choice | Concurrent

type expr = 
	{ exprname: string positioned 
	; condbinding: comma_item list 
	; successors: ident list
        ; etype: expr_type
	}

type err = 
	{ onnodes: ident list 
	; handler: string positioned
	}

type mod_inst = 
	{ modsourcename : string positioned 
	; modinstname : string positioned
        ; guardaliases : (string positioned * string positioned) list
	}

type program = 
	{ cond_decl_list: cond_decl list 
	; atom_decl_list: atom_decl list
	; node_decl_list: node_decl list
	; mainfun_list: mainfun list
	; expr_list: expr list 
	; err_list: err list
	; mod_def_list: mod_def list
	; mod_inst_list: mod_inst list
        ; terminate_list: string positioned list
	}
and mod_def = 
	{ modulename : string positioned 
	; programdef : program
	}

val trace_thing : string -> unit

val strip_position : string positioned -> string


