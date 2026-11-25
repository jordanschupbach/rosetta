// The Pascal API for tests/pascal_full_names.hpp: names alike but for case, keywords, the runtime's
// names, routines the unit uses, TObject's and TNativeObject's members, Result and Self, a renamed
// virtual method, multiple inheritance, C++ hiding, operators, 64-bit values and var parameters.
// Argument: the generated names.pas.
program test_pascal_names;

{$mode objfpc}{$H+}
{$modeswitch nestedprocvars}

uses
  {$ifdef unix}BaseUnix,{$endif} SysUtils, names;

type
  TShouting = class(TNamed)
  public
    function &Label: String; override;
  end;
  TPipe = class(TResource)
  public
    function Close2: String; override;
  end;
  TDeep = class(TLeaf)
  public
    function Describe: String; override;
  end;
  TLoud = class(TThing)
  public
    function &Label: String; override;
  end;

var
  Checks: Integer = 0;

procedure Check(Ok: Boolean; const What: String);
begin
  Inc(Checks);
  if not Ok then
  begin
    WriteLn(StdErr, 'check failed: ', What);
    Halt(1);
  end;
end;

function TShouting.&Label: String;
begin
  Result := UpperCase(inherited &Label);
end;

function TPipe.Close2: String;
begin
  Result := 'pipe ' + inherited Close2;
end;

function TDeep.Describe: String;
begin
  Result := 'deep ' + inherited Describe;
end;

function TLoud.&Label: String;
begin
  Result := 'loud ' + inherited &Label;
end;

function Inc1(A: Int32): Int32;
begin
  Result := A + 1;
end;

procedure WriteC;
begin
  Write('c');
end;

{$ifdef unix}
// Pascal's buffered Output and C++'s std::cout/stdout stay in order (fd 1 is a file, not a terminal).
procedure TestOutputOrder;
var
  Path, Written: String;
  Saved, Fd: cint;
  F: TextFile;
  Line: String;
begin
  Path := GetTempFileName;
  Flush(Output);
  Saved := FpDup(1);
  Fd := FpOpen(Path, O_WRONLY or O_CREAT or O_TRUNC, &644);
  FpDup2(Fd, 1);
  FpClose(Fd);
  Write('a');
  SayAround('b', @WriteC);
  WriteLn('d');
  Flush(Output);
  FpDup2(Saved, 1);
  FpClose(Saved);
  AssignFile(F, Path);
  Reset(F);
  Written := '';
  while not Eof(F) do
  begin
    ReadLn(F, Line);
    Written := Written + Line + '|';
  end;
  CloseFile(F);
  DeleteFile(Path);
  Check(Written = 'ab[c]|d|', 'output order with C++: ' + Written);
end;
{$endif}

procedure TestNames;
var
  N: TNativeObject_;
  E: TNativeError;
  R: TRosettaCall_;
  D: TData;
  P: TPoint;
  Q: TPoint_;
  C: TCursor;
begin
  // Classes named like the runtime's types, or alike but for case.
  N := TNativeObject_.Create;
  E := TNativeError.Create;
  R := TRosettaCall_.Create;
  Check((N.Native = 2) and (E.What = 'not an exception') and (R.Id = 3), 'the runtime''s names');
  Check((N is TNativeObject) and not (TObject(E) is Exception), 'class hierarchy');
  N.Free;
  E.Free;
  R.Free;
  D := TData.Create;
  Check((D.Value = 4) and (Data.Twice(4) = 8), 'a class before a policy module');
  D.Free;
  P := TPoint.Create;
  Q := TPoint_.Create;
  Check((PointX(P) = 1) and (PointY(Q) = 2), 'classes alike but for case');
  P.Free;
  Q.Free;

  // Keywords take &; routines the unit uses and names alike but for case take a number.
  Check((&Begin(4) = 4) and (&Repeat('ab') = 'abab') and (&In(1, 2) = 3), 'keywords');
  Check((Length2('abc') = 30) and (Copy2(1) = 2) and (Length('abc') = 3), 'the unit''s routines stay theirs');
  Check((Total(1) = 1) and (Total2(1, 2) = 3), 'functions alike but for case');

  // Enumerators.
  Check((ModeValue(TMode.&On) = 0) and (ModeValue(TMode.On2) = 1), 'enumerators alike but for case');
  Check((ModeValue(TMode.Self_) = 2) and (ModeValue(TMode.&Type) = 3) and (ModeValue(TMode.&End) = 4) and (ModeValue(TMode.Result_) = 5) and
        (ModeValue(TMode.Off) = 6), 'keywords, Self and Result as enumerators');
  Check((TMode.&Type.ToString = 'Type') and (TMode.On2.ToString = 'On2'), 'enumerator names');

  // Members.
  C := TCursor.Create;
  Check((C.Count = 20) and (C.Count2 = 2), 'a method before a field alike but for case');
  Check(C.Close2 and not C.IsClosed and (C.Cast2 = 'cast') and (C.Cast(TCursor) = C), 'TNativeObject''s members first');
  Check((C.ClassName2 = 'cursor') and (C.ClassName = 'TCursor') and C.Equals2(3) and C.Equals(C), 'TObject''s members first');
  Check((C.Free2 = 7) and (C.Destroy2 = 8) and (C.Result_ = 9), 'Free, Destroy and Result');
  Check((C.GetName = 'camel') and (C.GetName2(2) = 'snakesnake'), 'names alike but for case');
  Check((C.&Label(3) = 6) and (C.Move(4) = 4) and (C.Position = 4), 'keyword and fine method names');
  Check(TCursor.Create2 = 5, 'a class function named like the constructor');
  C.Close;
  Check(C.IsClosed, 'the generated Close');
  C.Free;
end;

procedure TestClasses;
var
  Res: TResource;
  F: TFile;
  Pipe: TPipe;
  Thing: TThing;
  Named: TNamed;
  Shout: TShouting;
  Loud: TLoud;
  Leaf: TLeaf;
  B: TBase;
  Deep: TDeep;
begin
  // A virtual method renamed in Pascal: the override has the Pascal name, and C++ calls it.
  Res := TResource.Create;
  F := TFile.Create;
  Pipe := TPipe.Create;
  Check((CloseResource(Res) = 'C++ close') and (CloseResource(F) = 'file close'), 'renamed virtual method');
  Check((CloseResource(Pipe) = 'pipe C++ close') and (F.Close2 = 'file close'), 'Pascal override of a renamed method');
  Res.Free;
  F.Free;
  Pipe.Free;

  // Multiple inheritance: Counted is the parent class; Named's members are declared again.
  Thing := TThing.Create;
  Check((Thing is TCounted) and (Thing.AsNamed.Cast(TThing) is TThing) and (Thing.AsNamed = Thing.AsNamed), 'both bases');
  Check((Thing.Counted = 3) and (CountedOf(Thing) = 3) and (Thing.Tag = 't'), 'the further base''s members');
  Check((Thing.&Label = 'thing') and (LabelOf(Thing.AsNamed) = 'thing'), 'a further base argument');
  Named := AsNamed(Thing);
  Check((Named.Cast(TThing) <> nil) and (LabelOf(Named) = 'thing'), 'a pointer to the further base');
  Named.Free;   // a borrowed pointer: frees only the Pascal object
  Thing.Free;   // and the objects it owns
  Shout := TShouting.Create;
  Loud := TLoud.Create;
  Check((LabelOf(Shout) = 'NAMED') and (LabelOf(Loud.AsNamed) = 'loud thing'), 'Pascal subclasses of either base');
  Shout.Free;
  Loud.Free;

  // C++ hiding: Pascal overloads Leaf.Scale with Base's; Leaf's Size takes another name.
  Leaf := TLeaf.Create;
  B := TBase.Create;
  Check((Leaf.Scale(2, 3) = 6) and (Leaf.Scale(2) = 2), 'overloads across classes');
  Check((Leaf.Size2 = 'big') and (Leaf.Size = 1) and (B.Size = 1), 'a method with another result takes its own name');
  Deep := TDeep.Create;
  Check((DescribeBase(Deep) = 'deep leaf') and (DescribeBase(Leaf) = 'leaf'), 'override two levels deep');
  Deep.Free;
  B.Free;
  Leaf.Free;
end;

procedure TestValues;
var
  Money, Other, R: TMoney;
  Padded: TPadded;
  Plain: TObject;
  N: Int32;
  Text: String;
  Pairs: TArrayOfPairOfStringAndInt32;
  T: TTupleOfInt32AndStringAndDouble;

  function Failing(A: Int32): Int32;
  begin
    raise ENativeError.Create(TNativeErrorKind.Binding, 'from a nested routine');
    Result := A;
  end;

begin
  // Operators, ToString and Invoke.
  Money := TMoney.Create(250);
  Check(Money.ToString = '2.50', 'to_string is ToString');
  Other := TMoney.Create(5);
  R := Money.Add(Other);
  Check(R.Cents = 255, 'operator method');
  R.Free;
  R := Money.Multiply(2);
  Check(R.Cents = 500, 'operator method with a scalar');
  R.Free;
  Check(Money.IsLess(TMoney(Other)) = False, 'comparison');
  Check(not Money.LogicalNot and (Money.Invoke(5) = 255), 'prefix operator and Invoke');
  Other.Free;
  Money.Free;
  Padded := TPadded.Create(7);
  Plain := Padded;
  Check((Padded.ToString = '007') and (Plain.ToString = '007') and (Padded.ToString(5) = '00007'), 'to_string with default arguments is ToString');
  Padded.Free;

  // Values.
  Check((Biggest = High(UInt64)) and (EchoU64(High(UInt64)) = High(UInt64)), 'unsigned 64-bit values');
  Check(EchoI64(Low(Int64)) = Low(Int64), 'signed 64-bit minimum');
  Check(not Negate(True) and Negate(False), 'booleans');
  Check((KindOf(True) = 'bool') and (KindOf(Int64(5)) = 'int') and (KindOf('5') = 'string'), 'booleans, numbers and strings stay apart');
  N := 1;
  Text := 'a';
  Bump(N, Text);
  Check((N = 2) and (Text = 'a!'), 'var parameters written back');
  Check(CallTwice(@Inc1, 1) = 3, 'routine');
  try
    CallTwice(@Failing, 1);
    Check(False, 'routine error');
  except
    on E: ENativeError do Check(E.Message = 'from a nested routine', 'a nested routine''s exception comes back');
  end;
  Pairs := Ranked(TArrayOfString.Create('a', 'b'));
  Check((Length(Pairs) = 2) and (Pairs[1].First = 'b') and (Pairs[1].Second = 2), 'array of pairs');
  T := Triple;
  Check((T.Item1 = 1) and (T.Item2 = 'two') and (T.Item3 = 3.5), 'tuple record');
  T := TTupleOfInt32AndStringAndDouble.Make(4, 'x', 1);
  Check(T.Item2 = 'x', 'tuple Make');
end;

begin
  Check(ExtractFileName(ParamStr(1)) = 'names.pas', 'generated unit path');
  TestNames;
  TestClasses;
  TestValues;
  {$ifdef unix}TestOutputOrder;{$endif}
  WriteLn('Pascal names passed (', Checks, ' checks)');
end.
