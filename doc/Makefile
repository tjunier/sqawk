.PHONY: clean, distclean

sqawk_talk.pdf: sqawk_talk.tex
	 texexec --mode=presentation $<

clean:
	$(RM) *.mp *.log *.tui *.tuo *.tmp

distclean: clean
	$(RM) *.pdf
