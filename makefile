# MIT License
#
# Copyright (c) 2021 Caio Alves Garcia Prado
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
#############################################################################
#
# Generic makefile intended for C/C++/Fortran projects.
#
# Coupled with makedepend.py, it attempts to automatically setup linking
# dependencies (which .o files are necessary for linking a certain program?)
# without ever having to manually modify the makefile itself.
# Note that header dependencies (which .h files are necessary to compile a
# given .c file) are not dealt with directly and instead are taken care of by
# GCC's -MMD -MP -MF flags.
#
# In order for the setup to work, a couple of conventions must be followed.
#  • Name source files containing 'main()' function or 'PROGRAM' subroutine
#    as 'main_name.*', which will generate binary files 'bin/name'
#  • Put all header files (.h, .hpp, .inc, ...) inside 'include/' directory
#  • Put all source files (.c, .cpp, ...) inside 'src/' directory
#  • For Fortran only, 'name_*.f' are set as dependencies of 'bin/name'
#
# Targets include:
#  cleanbuild  delete all temporary files (*.o, *.mod)
#  clean       cleanbuild + delete binary programs (bin/*)
#  cleanall    clean + delete dependency information (.d/)
#  [default]   when called without target, builds every 'main_name.*'
#
#############################################################################

# • Variables                                                            {{{1
#  - paths                                                               {{{2

BASEDIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
INCDIR   := $(BASEDIR)/include
SRCDIR   := $(BASEDIR)/src
DEPDIR   := $(BASEDIR)/.d
BINDIR   := $(abspath .)/bin
ALLDIRS  := $(INCDIR) $(SRCDIR) $(DEPDIR) $(BINDIR)

#  - programs                                                            {{{2

CC       := gcc
CXX      := g++
FC       := gfortran
LD       := g++
MKDEPEND := $(BASEDIR)/makedepend.py
MKDIR    := mkdir -p
RM       := rm -f
SHELL    := /bin/sh

#  - functions                                                           {{{2

interact := $(if $(strip $(MAKE_TERMOUT)$(MAKE_TERMERR)),1)
color     = $(if $(interact),[3$(word $1, 4 6 1)m$2[39m,$2)
bold      = $(if $(interact),[1m$1[m,$1)
level     = $(if $(MSGLEVEL),$(word $(words $(MSGLEVEL)), ╰─> ├) )
format    = $1$(if $2, $(call color,$3,‘$(notdir $2)’))…
msg       = $(info $(call bold,$(level)$(call format,$1,$2,$3)))

#  - flags                                                               {{{2

DEFBASE  := -D'BASEDIR=$(BASEDIR)/'

CFLAGS   += -O2
CPPFLAGS += -Wall -Wno-misleading-indentation -I$(INCDIR) $(DEFBASE)
CXXFLAGS += -g -O4 -fPIC -flto -pthread -std=c++20 -m64
LDFLAGS  += -Wl,--as-needed $(CXXFLAGS)

FFLAGS   += -I$(INCDIR) -g -fno-underscoring
FFLAGS   += -fbacktrace -ffpe-trap=zero,overflow,underflow,invalid
FPPFLAGS += $(DEFBASE)

LIBS      = -llzma
DEPFLAGS  = -MT $@ -MMD -MP -MF $(DEPDIR)/$(notdir $(basename $<)).d

#  - targets                                                             {{{2

find_src  = $(foreach EXT, $1, $(wildcard $(SRCDIR)/*.$(EXT)))
CSOURCES := $(call find_src, c cpp)
FSOURCES := $(call find_src, f f90 F F90)
SOURCES  := $(CSOURCES) $(FSOURCES)
MAINSRC  := $(filter $(SRCDIR)/main_%,$(SOURCES))
FMAINSRC := $(filter $(SRCDIR)/main_%,$(FSOURCES))

OBJECTS  := $(addsuffix .o,$(basename $(SOURCES)))
FORMODS  := $(addsuffix .mod,$(basename $(SOURCES)))

DEPFILE  := $(DEPDIR)/depfile
DEPENDS  := $(patsubst $(SRCDIR)/%,$(DEPDIR)/%,$(OBJECTS:.o=.d)) $(DEPFILE)

TARGETS  := $(patsubst $(SRCDIR)/main_%,$(BINDIR)/%,$(basename $(MAINSRC)))
FTARGETS := $(patsubst $(SRCDIR)/main_%,$(BINDIR)/%,$(basename $(FMAINSRC)))

# • General rules                                                        {{{1

all: $(TARGETS)
.PHONY: all

# create directories
$(ALLDIRS):
	@$(call msg,Create path,$@,1)
	@$(MKDIR) $@

# message level
$(TARGETS) $(OBJECTS) $(DEPENDS) $(ALLDIRS): MSGLEVEL+=-

# • Cleaning rules                                                       {{{1

# clean intermediate
cleanbuild:
	@$(call msg,Clean temporaries)
	@$(RM) $(OBJECTS) $(FORMODS)
.PHONY: cleanbuild

# clean intermediate + target
clean: cleanbuild
	@$(call msg,Clean targets)
	@$(RM) -r $(TARGETS) $(BINDIR)
.PHONY: clean

# clean intermediate + target + dependencies
cleanall: clean
	@$(call msg,Clean dependencies)
	@$(RM) -r $(DEPDIR)
.PHONY: cleanall

# • Compilation rules                                                    {{{1

# linking the final executable
$(TARGETS): $(BINDIR)/%: $(SRCDIR)/main_%.o | $(BINDIR)
	@$(call msg,Link executable,$@,3)
	@$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
$(FTARGETS): LIBS += -lgfortran

# for Fortran, let's assume that binary 'name' links to all files name_*.o
.SECONDEXPANSION:
SUBPAT := %
$(FTARGETS): $(BINDIR)/%: $$(filter $(SRCDIR)/%_$$(SUBPAT),$(OBJECTS))

# compiling source code: generate .o and .d files from sources
# .d files are created together with .o files: this makes .d always
#   updated for next run.  (modification time of .d should be before
#   .o, in case of gcc)
# .d should not depend on the sources: not necessary, since sources
#   changes will force .o to be rebuilt (and in turn, .d to be updated).
# .d files are dependencies of .o files: if user deletes .d, it will
#   be rebuilt.
# .d files have empty rule: make will assume they are always created/
#   updated instead of complaining it can'd do it.
$(SRCDIR)/%.o: $(SRCDIR)/%.c $(DEPDIR)/%.d
	@$(call msg,Compile,$<,2)
	@$(CC) $(DEPFLAGS) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp $(DEPDIR)/%.d
	@$(call msg,Compile,$<,2)
	@$(CXX) $(DEPFLAGS) $(CPPFLAGS) $(CXXFLAGS) -o $@ -c $<

# compile fortran code: 4 different file extensions and 2 "flavors"
define fortran
$(SRCDIR)/%.o $(SRCDIR)/%.mod: $(SRCDIR)/%.$1
	@$$(call msg,Compile,$$<,2)
	@$(FC) $2 $(FFLAGS) -J$(SRCDIR) -o $$(basename $$@).o -c $$<
endef
$(eval $(call fortran,f))
$(eval $(call fortran,f90))
$(eval $(call fortran,F,   $(FPPFLAGS)))
$(eval $(call fortran,F90, $(FPPFLAGS)))

# • Dependencies management                                              {{{1

# ensure depdir always exists so make won't ignore recipes depending on it
$(DEPENDS): | $(DEPDIR)
.PRECIOUS: %.d

# create dependency tree for linking final executable
$(DEPFILE): $(MKDEPEND) $(MAINSRC) $(FSOURCES)
	@$(call msg,Update dependencies)
	@$(MKDEPEND) -I $(INCDIR) -s $(SRCDIR) -o $@ $(MAINSRC) $(FSOURCES)

# if MAKECMDGOALS is not 'clean*' include dependencies to Makefile:
ifneq ($(sort $(patsubst clean%,clean,$(MAKECMDGOALS))),clean)
  -include $(wildcard $(DEPENDS)) $(DEPFILE)

  # for all relative paths in goals, set as dependency the absolute path file
  # this is a hack to make dependencies work when file uses relative path
  MAKECMDGOALS  :=  $(filter-out /%,$(MAKECMDGOALS))
  $(MAKECMDGOALS): $(abspath $(MAKECMDGOALS)) ;
endif
