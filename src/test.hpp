#pragma once

#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/variant/string.hpp"

using namespace godot;

class Test: public godot::Node{
    GDCLASS(Test, Node)
protected:
    static void _bind_methods();
    //always impliment this
private:
    String my_data ="Seth is cool";
    String get_my_data() const; //constructor at end because they do edit anything that belongs to class
    void set_my_data(const String &new_data); //& means dont copy the string, just ref the object.

    void say_hello();

};

