% name@features.Circle (obj) runs the C++ method instead of dispatching back.
classdef Loud < features.Circle
  methods
    function obj = Loud (r)
      obj@features.Circle (r);
    end
    function r = name (obj)
      r = upper (name@features.Circle (obj));
    end
  end
end
