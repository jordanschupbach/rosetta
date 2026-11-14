% An Octave subclass implementing C++ virtual methods (a director).
classdef Triangle < features.Shape
  properties
    base_length = 0
    height = 0
  end
  methods
    function obj = Triangle (b, h)
      obj@features.Shape ();
      obj.base_length = b;
      obj.height = h;
    end
    function r = area (obj)
      r = obj.base_length * obj.height / 2;
    end
    function r = name (obj)
      r = 'triangle';
    end
  end
end
