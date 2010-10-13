#ifndef MODULELOADER_H
#define MODULELOADER_H

#include <QtDebug>
#include <QCoreApplication>
#include <QString>

#include <pulse/context.h>
#include <pulse/glib-mainloop.h>
#include <pulse/introspect.h>
#include <pulse/operation.h>
#include <strings.h>

#include <iostream>
#include <sstream>

class Module{

private:
	bool tunnel_flag;
	bool combine_flag;

public:
	Module(){
		tunnel_flag = false;
		combine_flag = false;
	}

	void set_loaded_tunnel_module(bool flag);
	void set_loaded_combine_module(bool flag);
	
	void load_module_tunnel(pa_context *, pa_operation*, QString );
	
	void load_module_combine(pa_context *, pa_operation*, QList<uint32_t>);
	
	static void load_module_cb(pa_context *, uint32_t , void *);
	
	template <typename T> 
	std::string to_string(const T& value)
	{
		std::stringstream oss;
		oss << value;
		return oss.str();
	}

};

#endif
