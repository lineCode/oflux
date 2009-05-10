$(warn Reading rules.mk)

CC := gcc 
#
# Be paranoid about building archives and delete any prior versions
# before building new versions.  Not doing this can lead to archives
# containing stale code under some circumstances.
#
%.a:
	$(RM) $@
	$(AR) -q $@ $^

%.gch: $(HEADERS)  
	$(CXX) $(CPPFLAGS) $^ -o $@

CPPFLAGS += $(BASECXXFLAGS) $(COMPONENT_FLAGS) $(ARCH_FLAGS) $(INCS) -D$(_ARCH) 
BOOSTDIR := /oanda/system/include/boost-1_34_1

BASECXXFLAGS = -Wall -D_REENTRANT -ggdb -O0 -MMD -MF "$(@F:.o=.d)"
OFLUX_RUNTIME = $(SRCDIR)/runtime/c++.1

INCS = \
    -I/oanda/system/include \
    -I/oanda/system/include/boost-1_34_1 \
    -I$(BOOSTDIR) \
    -I$(SRCDIR)/private_include \
    -I$(SRCDIR)/include \
    -I$(SRCDIR)/src \
    -I$(OFLUX_RUNTIME) \
    -I.

LIBS= -lexpat -lpthread -ldl
LIBDIRS= -L.

# Include any specific settings for this RELEASE (eg. production, debug, etc)
-include $(SRCDIR)/Mk/$(RELEASE).mk

# Include any specific settings for this architecture (eg. Linux, Sparc, etc)
-include $(SRCDIR)/Mk/$(_ARCH).mk

%.pic.o: %.cpp
	$(CXX) $(CPPFLAGS) -c -fPIC $(CXXFLAGS) $< -o $@

# OFLUX
OFLUXCOMPILER := $(CURDIR)/oflux

#OFluxGenerate_%.h OFluxGenerate_%.cpp %.dot : %.flux oflux
	#$(OFLUXCOMPILER) -a $* $(OFLUX_INCS) $*.flux

# OCAML
OCAMLCOMPILER:=ocaml$(if $(findstring dev,$(OCAMLCONFIG)),c -g,opt) $(if $(findstring profile,$(OCAMLCONFIG)),-p,)
OBJECTEXT:=cm$(if $(findstring dev,$(OCAMLCONFIG)),o,x)
LIBRARYEXT:=cm$(if $(findstring dev,$(OCAMLCONFIG)),,x)a
OCAMLDEP:=ocamldep $(if $(findstring dev,$(OCAMLCONFIG)),, -native)


%.cmi: %.mli 
	$(OCAMLCOMPILER) $(INCLUDEOPTS) -o $@ -c $<

%.$(OBJECTEXT): %.ml 
	$(OCAMLCOMPILER) $(INCLUDEOPTS) -o $@ -c $<


DOTCOMMAND:= $(shell which dot | grep -v no)
DOT:=$(if $(DOTCOMMAND),$(DOTCOMMAND) -Tsvg,)

DOXYGENCOMMAND:= $(shell which doxygen | grep -v no)
DOXYGEN:=$(if $(DOXYGENCOMMAND),$(DOXYGENCOMMAND), echo "install doxygen ")

# dtrace
DTRACE:=$(shell which dtrace | grep -v no)
CPPFLAGS += $(if $(DTRACE),-DHAS_DTRACE,)
DTRACE_LIB_PROBE_HEADER:=$(if $(DTRACE),ofluxprobe.h,)
DTRACE_SHIM_PROBE_HEADER:=$(if $(DTRACE),ofluxshimprobe.h,)
