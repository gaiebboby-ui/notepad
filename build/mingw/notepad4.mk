# Makefile for notepad4

PROJ = Notepad
NAME = $(BINFOLDER)/$(PROJ).exe
OBJDIR = $(BINFOLDER)/obj/$(PROJ)
SRCDIR = ../../src
editlexers_dir = $(SRCDIR)/EditLexers
scintilla_dir = ../../scintilla

INCDIR = \
	-I"../../src" \
	-I"../../src/EditLexers" \
	-I"$(scintilla_dir)/include"

LDFLAGS += -L"$(BINFOLDER)/obj"

LDLIBS += -limm32 -ldwmapi

editlexers_src = $(wildcard $(editlexers_dir)/*.cpp)
editlexers_obj = $(patsubst $(editlexers_dir)/%.cpp,$(OBJDIR)/%.obj,$(editlexers_src))

# c_src = $(wildcard $(SRCDIR)/*.c)
# c_obj = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.obj,$(c_src))

cpp_src = $(wildcard $(SRCDIR)/*.cpp)
cpp_obj = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.obj,$(cpp_src))

md4c_dir = $(SRCDIR)/md4c
md4c_names = entity md4c md4c-html
md4c_obj = $(addprefix $(OBJDIR)/md4c-,$(addsuffix .obj,$(md4c_names)))

rc_src = $(wildcard $(SRCDIR)/*.rc)
rc_obj = $(patsubst $(SRCDIR)/%.rc,$(OBJDIR)/%.res,$(rc_src))

all: $(NAME)

$(NAME): $(editlexers_obj) $(cpp_obj) $(md4c_obj) $(rc_obj)
	$(CXX) $^ $(LDFLAGS) -lscintilla $(LDLIBS) -o $@

$(editlexers_obj): $(OBJDIR)/%.obj: $(editlexers_dir)/%.cpp
	$(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $(INCDIR) $< -o $(OBJDIR)/$*.obj

# $(c_obj): $(OBJDIR)/%.obj: $(SRCDIR)/%.c
# 	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCDIR) $< -o $(OBJDIR)/$*.obj

$(cpp_obj): $(OBJDIR)/%.obj: $(SRCDIR)/%.cpp
	$(CXX) -c $(CPPFLAGS) $(CXXFLAGS) $(INCDIR) $< -o $(OBJDIR)/$*.obj

$(OBJDIR)/md4c-%.obj: $(md4c_dir)/%.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCDIR) -I"$(md4c_dir)" $< -o $@

$(rc_obj): $(OBJDIR)/%.res: $(SRCDIR)/%.rc
	$(RC) -c 65001 $(CPPFLAGS) $(RCFLAGS) $< $(OBJDIR)/$*.res

clean:
	@$(RM) -rf $(OBJDIR)
	@$(RM) -f $(NAME)
