
open ParserTypes

module StringKey =
struct 
	type t = string
	let compare = compare
end

module StringMap = Map.Make(StringKey)

type node_data = 
                { functionname: string
		; nodeinputs: decl_formal list 
		; nodeoutputs: decl_formal list option
		; nodeguardrefs: guardref list
		; where: position 
		; nodedetached: bool 
                ; nodeabstract: bool
                }

type conditional_data = 
		{ arguments: decl_formal list 
		; cfunction: string
		}

type guard_data = { garguments: decl_formal list
		; gtype: string
		; return: data_type 
		; magicnumber: int }

type module_inst_data =
		{ modulesource : string }

type symbol = 
	Node of node_data
	| Conditional of conditional_data
	| Guard of guard_data
	| ModuleDef of module_def_data
	| ModuleInst of module_inst_data
and module_def_data = { modulesymbols : symbol_table }
and symbol_table = symbol StringMap.t

let empty_symbol_table = StringMap.empty

(* conditional functions *)

let add_conditional symtable c =
	StringMap.add (strip_position c.condname)
		(Conditional { arguments=c.condinputs; cfunction=c.condfunction }) symtable

let lookup_conditional_symbol symtable name =
	match StringMap.find name symtable with
		(Conditional x) -> x
		| _ -> raise Not_found

let fold_conditionals f symtable w =
	let smf n sym w =
		match sym with
			(Conditional nd) -> f n nd w
			| _ -> w
	in  StringMap.fold smf symtable w

(* guard functions *)

let next_magic_no = ref 1

let add_guard symtable g =
	let mn = !next_magic_no in
	let gname = strip_position g.atomname in
	let _ = Debug.dprint_string ("guard "^gname^" added to sym table \n") in
	let res = StringMap.add gname (Guard 
		{ garguments=g.atominputs
		; return=g.outputtype
		; gtype=g.atomtype
		; magicnumber = mn}) symtable in
	let _ = next_magic_no := mn+1
	in  res

let lookup_guard_symbol symtable name =
	match StringMap.find name symtable with
		(Guard x) -> x
		| _ -> raise Not_found

let fold_guards f symtable w =
	let smf n sym w =
		match sym with
			(Guard g) -> f n g w
			| _ -> w
	in  StringMap.fold smf symtable w

(* node symbols *)

let add_node_symbol symtable n =
	let name,pos,_ = n.nodename
	in  StringMap.add name (Node 
		{ functionname=n.nodefunction
		; nodeinputs=n.inputs
		; nodeoutputs=n.outputs
		; nodeguardrefs=n.guardrefs
		; where=pos
		; nodedetached=n.detached 
                ; nodeabstract=(n.abstract || n.outputs = None)
                }) symtable

let lookup_node_symbol symtable name =
	match StringMap.find name symtable with
		(Node x) -> x
		| _ -> raise Not_found
		
let fold_nodes f symtable w =
	let smf n sym w =
		match sym with
			(Node nd) -> f n nd w
			| _ -> w
	in  StringMap.fold smf symtable w

let lookup_node_symbol_from_function symtable name =
	let ffun n nd son =
		match son with
			None -> if nd.functionname = name then Some nd else None
			| _ -> son
	in  match fold_nodes ffun symtable None with
		None -> raise Not_found
		| (Some x) -> x

let is_detached symtable name =
	let x = lookup_node_symbol symtable name
	in  x.nodedetached

let is_abstract symtable name =
	let x = lookup_node_symbol symtable name
	in  x.nodeabstract

exception DeclarationLookup of string

let get_decls stable (name,isin) =
	try let nd = lookup_node_symbol_from_function stable name
	    in  if isin then nd.nodeinputs 
	    	else (match nd.nodeoutputs with
			None -> raise (DeclarationLookup ("node "^name^" is abstract and cannot be used for code generation"))
			| (Some x) -> x)
	with Not_found -> raise (DeclarationLookup ("could not resolve node "^name))

(* deprecated
* so called typedefs *

let add_type_def symtable ((typename,tnamepos,_),def) =
	StringMap.add typename (Typedef (def,ref None,tnamepos)) symtable

let update_arg_type symtable typename argtype =
	let _,arg_type,_ = (StringMap.find typename symtable) in
	let _ = arg_type := Some argtype
	in ()

let lookup_typedef_symbol symtable name =
	match StringMap.find name symtable with
		(Typedef (n1,n2,n3)) -> (n1,n2,n3)
		| _ -> raise Not_found
*)



(** modules *)

let add_module_instance symtable mi =
	let name,pos,_ = mi.modinstname
	in  StringMap.add name (ModuleInst 
		{ modulesource= strip_position mi.modsourcename }) symtable

let rec add_module_definition symtable md =
	let name, pos, _ = md.modulename in
	let msymtable = add_program empty_symbol_table md.programdef
	in  StringMap.add name (ModuleDef
		{ modulesymbols = msymtable }) symtable
(** programs *)
and add_program symboltable p =
        let node_decls = p.node_decl_list in
        let cond_decls = p.cond_decl_list in
        let atom_decls = p.atom_decl_list in
	let module_insts = p.mod_inst_list in
	let module_decls = p.mod_def_list in
        let symboltable = List.fold_left add_node_symbol symboltable                node_decls in
        let symboltable = List.fold_left add_conditional symboltable                cond_decls in
        let symboltable = List.fold_left add_guard symboltable
                atom_decls in
        let symboltable = List.fold_left add_module_instance symboltable
                module_insts in
        let symboltable = List.fold_left add_module_definition symboltable
                module_decls 
	in  symboltable

let lookup_module_definition symtable name =
	match StringMap.find name symtable with
		(ModuleDef x) -> x
		| _ -> raise Not_found

let lookup_module_instance symtable name =
	match StringMap.find name symtable with
		(ModuleInst x) -> x
		| _ -> raise Not_found

let fold_module_definitions f symtable w =
	let smf n sym w =
		match sym with
			(ModuleDef nd) -> f n nd w
			| _ -> w
	in  StringMap.fold smf symtable w

let fold_module_instances f symtable w =
	let smf n sym w =
		match sym with
			(ModuleInst nd) -> f n nd w
			| _ -> w
	in  StringMap.fold smf symtable w

(* basic type unification *)

type unify_result =
	Success
	| Fail of int * string 
		(* ith, reason *)

let strip_position3 df = 
	( strip_position df.ctypemod
	, strip_position df.ctype
	, strip_position df.name) 

let unify_single (ctm1,t1,_) (ctm2,t2,_) =
	match (t1 = t2), ctm1, ctm2 with
		(false,_,_) -> Some "mismatched types"
		| (_,"const","") -> None
		| (_,"","const") -> Some ("cannot drop const")
		| _ -> 
			if ctm1 = ctm2 then None
			else Some ("mismatched type modifiers")

let unify_type_in_out (tl_in: decl_formal list) (tl_out: decl_formal list) =
	let rec unify_type_in_out' i tl_in tl_out =
		match tl_in, tl_out with
			([],[]) -> Success
			| (tinh::tintl,touh::toutl) ->
				(match unify_single
						(strip_position3 tinh) 
						(strip_position3 touh) with
					None -> unify_type_in_out' (i+1) tintl toutl
					| (Some reason) -> Fail (i,reason))
			| _ -> Fail (i,"one more argument between unified parameter lists")
	in  unify_type_in_out' 0 tl_in tl_out
					
let unify' (io1,io2) symtable node1 node2 =
	let pick io x = if io then Some x.nodeinputs else x.nodeoutputs
	in  try let nd1 = lookup_node_symbol symtable node1 in
		let nd2 = lookup_node_symbol symtable node2
		in  match pick io1 nd1, pick io2 nd2 with
			(Some as1, Some as2) -> unify_type_in_out as1 as2
			| _ -> Success (* weak *)
	    with Not_found->
		Fail (-1,"node not found in symbol table")

let unify symtable node1 node2 = unify' (true,false) symtable node1 node2



