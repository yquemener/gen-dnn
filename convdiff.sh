diff -bBU2 src/cpu/jit_primitive_conf.hpp src/vanilla/any_primitive_conf.hpp
for v in `find src/vanilla`; do
	c=src/cpu/`basename $v`
	if [ -f "$v" -a -f "$c" ]; then
		diff -bBU2 $c $v
	fi
done