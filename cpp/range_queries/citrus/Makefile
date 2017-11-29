
EER_PRCU=	eer_prcu.c prcu.h
DEER_PRCU=	deer_prcu.c prcu.h
D_PRCU=		d_prcu.c prcu.h

all: libeerprcu.so libdeerprcu.so libdprcu.so

libeerprcu.so: $(EER_PRCU)
	gcc -shared -O3 -fPIC -o $@ -I. $(EER_PRCU) -lpthread

libdeerprcu.so: $(DEER_PRCU)
	gcc -shared -O3 -fPIC -DRCU_HAS_RANGE -o $@ -I. $(DEER_PRCU) -lpthread

libdprcu.so: $(D_PRCU)
	gcc -shared -O3 -fPIC -DRCU_HAS_RANGE -o $@ -I. $(D_PRCU) -lpthread

clean:
	rm -rf libeerprcu.so libdeerprcu.so libdprcu.so

