% Implements a callback interface with default implementations.
classdef Summer < features.Visitor
  properties
    seen = []
  end
  methods
    function r = visit (obj, value)
      r = value * 10;
    end
    function finished (obj, values)
      obj.seen = values;
    end
  end
end
