$(call add-hdrs,fd_quic.h)
$(call add-objs,fd_quic_tile,fd_disco)
$(call make-unit-test,test_quic_tile,test_quic_tile,fd_quic fd_tls fd_disco fd_tango fd_ballet fd_util)
