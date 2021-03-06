$(info Reading oflux rules.mk)

CC := gcc 

ARFLAGS := crv
#
# Be paranoid about building archives and delete any prior versions
# before building new versions.  Not doing this can lead to archives
# containing stale code under some circumstances.
#
%.a:
	$(RM) $@
	$(AR) $(ARFLAGS) $@ $^

%.gch: $(HEADERS)  
	$(CXX) $(CPPFLAGS) $^ -o $@

# CPPFLAGS are the flags that will be given to the C Preprocessor
# Place things here that will alter what will be compiled
CPPFLAGS = $(COMPONENT_FLAGS) $(ARCH_FLAGS) $(INCS) -D$(_ARCH) -D_REENTRANT

# CXXFLAGS are the flags that will be given to the C++ Compiler
# Place things here that will alter how the compilation will occur
CXXFLAGS = $(BASECXXFLAGS) $(_OPTIMIZATION_FLAGS) $(DEBUG_FLAGS) $(WARN_FLAGS)

BOOSTDIR ?= /oanda/system/include/boost-1_34_1

DEBUG_FLAGS := -ggdb
WARN_FLAGS = -Wall

BASECXXFLAGS = -MMD -MF "$(@F:.o=.depend)"
OFLUXSRCDIR ?= $(SRCDIR)
OFLUX_RUNTIME = $(OFLUXSRCDIR)/src/runtime

INCS = \
    -I$(BOOSTDIR) \
    -I/oanda/system/include \
    -I$(OFLUX_RUNTIME) \
    -I.

LIBS= -lexpat -lpthread -ldl
LIBDIRS= -L. -L/oanda/system/lib

# Include any specific settings for this RELEASE (eg. production, debug, etc)
-include $(OFLUXSRCDIR)/Mk/$(RELEASE).mk

# Include any specific settings for this architecture (eg. Linux, Sparc, etc)
-include $(OFLUXSRCDIR)/Mk/$(_ARCH).mk

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
ifeq ($(_ARCH),SunOS)
DTRACE:=$(shell which dtrace | grep -v no)
ARCH_FLAGS += $(if $(DTRACE),-DHAS_DTRACE,)
DTRACE_LIB_PROBE_HEADER:=$(if $(DTRACE),ofluxprobe.h,)
DTRACE_SHIM_PROBE_HEADER:=$(if $(DTRACE),ofluxshimprobe.h,)
ifeq ($(OPTIMIZATION_FLAGS),-O0)
DTRACE_GCC_OPTIMIZATIONS:=-O0
else
DTRACE_GCC_OPTIMIZATIONS:=-O1 -finline-functions
endif
endif
-include $(OFLUXSRCDIR)/Mk/$(_PROC).mk
