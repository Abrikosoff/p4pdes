include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules

balanced: balanced.o  chkopts
	-${CLINKER} -o balanced balanced.o  ${PETSC_LIB}
	${RM} balanced.o

# etc

.PHONY: distclean

distclean:
	@rm -f *~ balanced

