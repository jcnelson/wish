NAME=wish

TARGET=$(NAME)-cos518
BIBTEX := bibtex
TGIF   := tgif
XFIG   := xfig
GNUPLOT:= gnuplot

SOURCES=$(NAME)-cos518.tex \
	abstract.tex \
	intro.tex \
	design.tex \
	motivation.tex \
	eval.tex \
	related.tex \
	conclusion.tex \
	comm.tex \
	local.bib \
	figures/ex1.tex \
	figures/ex2.tex \
	figures/ex3.tex \
# FIGS=	data/one.pdf \
#	data/two.pdf

all: $(TARGET).ps
pdf: all

$(TARGET).pdf: Makefile  $(SOURCES) $(FIGS)
	pdflatex  $(TARGET).tex
	-bibtex --min-crossrefs=100    $(TARGET)
	pdflatex  $(TARGET).tex

color: $(TARGET).pdf
	pdflatex $(TARGET).tex
	pdf2ps $(TARGET).pdf

$(TARGET).ps: $(TARGET).pdf
	pdflatex "\newcommand{\wantBW}{} \input{$(TARGET).tex}"
	pdf2ps $(TARGET).pdf

%.pdf : %.fig #Makefile
	fig2dev -L pdf -b 1 $< $@

%.eps : %.dia #Makefile
	dia --nosplash -e $@ $<

%.eps : %.obj
	TMPDIR=/tmp $(TGIF) -print -eps $<

%.pdf : %.eps #Makefile
	epstopdf $<

clean:
	rm -f *.aux *.log *.out *.bbl *.blg *~ *.bak $(FIGS) $(TARGET).ps $(TARGET).pdf
