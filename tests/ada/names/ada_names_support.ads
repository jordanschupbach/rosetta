--  Ada type extensions and subprograms for test_ada_names (library level).
with Names; use Names;

package Ada_Names_Support is
   type Shouting_Record is new Named_Record with null record;
   overriding function Label (Self : Shouting_Record) return String;

   type Pipe_Record is new Resource_Record with null record;
   overriding function Close_2 (Self : in out Pipe_Record) return String;

   type Deep_Record is new Leaf_Record with null record;
   overriding function Describe (Self : Deep_Record) return String;

   type Loud_Record is new Thing_Record with null record;
   overriding function Label (Self : Loud_Record) return String;

   function Inc1 (A : Integer) return Integer;
   function Failing (A : Integer) return Integer;
end Ada_Names_Support;
