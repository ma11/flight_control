function read_config

global fc
global ser

for n = 1:length(fc.flash_addr_list)
	srl_write(ser,sprintf("%s",[0,fc.flash_addr_list(n),0,0,0,0]));
	r = sum(double(srl_read(ser,4)) .* 2.^(24:-8:0));
	if fc.flash_float_list(n)
		f = typecast(uint32(r),'single');
		fprintf('%s = %f\n', fc.flash_name_list{n}, f);
	else
		fprintf('%s = %d\n', fc.flash_name_list{n}, r);
	end
end

end