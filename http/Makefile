TARGETS = server load_balancer test smtp

all: $(TARGETS)


%.o: %.cc %.h
	g++ -c $< -g -o $@

server: server.o template_render.o model.o email_api.o storage_api.o storage_request.o cache.o frontend_http.o
	g++ $^ -lpthread -luuid -lresolv -lssl -lcrypto -g -o $@

server.o: server.cc
	g++ -c $< -o $@

smtp: smtp.o template_render.o model.o email_api.o storage_api.o storage_request.o cache.o
	g++ $^ -lpthread -luuid -lresolv -lssl -lcrypto -g -o $@

smtp.o: smtp.cc
	g++ -c $< -o $@

template_render.o: template_render.cc
	g++ -c $< -o $@

model.o: model.cc
	g++ -c $< -o $@

storage_api.o: storage_api.cc
	g++ -c $< -o $@ 

email_api.o: email_api.cc
	g++ -c $< -lresolv -lssl -lcrypto -o $@

storage_request.o: storage_request.cc
	g++ -c $< -o $@ 

cache.o: cache.cc
	g++ -c $< -o $@ 

test: test.o template_render.o model.o email_api.o storage_api.o storage_request.o cache.o
	g++ $^ -lpthread -luuid -lresolv -lssl -lcrypto -g -o $@




test.o: test.cc
	g++ -c $< -o $@
# test.o: email_api.h test.cc 
# 	g++ -c $< -o $@

load_balancer: load_balancer.cc frontend_http.o 
	g++ $^ -lpthread -g -o $@ 	

frontend_http.o: frontend_http.cc
	g++ -c $< -o $@

# pop3: pop3.cc
# 	g++ $^ -I/usr/local/opt/openssl/include -L/usr/local/opt/openssl/lib -lcrypto -lpthread -g -o $@

pack:
	rm -f submit-hw2.zip
	zip -r submit-hw2.zip *.cc README Makefile

clean::
	rm -fv $(TARGETS) *~

realclean:: clean
	rm -fv submit-hw2.zip
