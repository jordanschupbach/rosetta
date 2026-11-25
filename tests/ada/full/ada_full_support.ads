--  Ada type extensions and subprograms for test_ada_full (library level, as Ada requires for
--  objects C++ keeps and subprograms passed to C++).
with Ada.Strings.Unbounded;
with Features; use Features;

package Ada_Full_Support is
   Sentinel : exception;

   type Triangle_Record is new Shape_Record with record
      B, H : Long_Float := 0.0;
   end record;
   overriding function Area (Self : Triangle_Record) return Long_Float;
   overriding function Name (Self : Triangle_Record) return String;
   type Triangle is access all Triangle_Record'Class;

   type Incomplete_Record is new Shape_Record with null record;

   type Failing_Record is new Shape_Record with null record;
   overriding function Area (Self : Failing_Record) return Long_Float;

   type Loud_Record is new Circle_Record with null record;
   overriding function Name (Self : Loud_Record) return String;

   type Noisy_Record is new Circle_Record with null record;
   overriding function Name (Self : Noisy_Record) return String;
   type Louder_Record is new Noisy_Record with null record;

   type Unit_Shape_Record is new Shape_Record with record
      Tag : Ada.Strings.Unbounded.Unbounded_String;
   end record;
   overriding function Area (Self : Unit_Shape_Record) return Long_Float;

   type Summer_Record is new Visitor_Record with record
      Seen : Long_Float_Vector;
   end record;
   overriding function Visit (Self : in out Summer_Record; Value : Long_Float) return Long_Float;
   overriding procedure Finished (Self : in out Summer_Record; Arg_1 : Long_Float_Vector);
   type Summer is access all Summer_Record'Class;

   function Twice (A : Long_Float) return Long_Float;
   function Plus_One (A : Long_Float) return Long_Float;
   function Failing (A : Long_Float) return Long_Float;
   function Via_Native (A : Long_Float) return Long_Float;
   Calls : Natural := 0;
   function Counting (A : Long_Float) return Long_Float;
end Ada_Full_Support;
