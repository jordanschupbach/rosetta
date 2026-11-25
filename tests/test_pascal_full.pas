// Pascal API (--language pascal) on the library-shaped example shared with the Python, Lua, Common
// Lisp, Julia, C#, VB, Go, Rust, PHP, Perl and Swift backends. Argument: the generated features.pas,
// for the documentation checks.
program test_pascal_full;

{$mode objfpc}{$H+}
{$modeswitch nestedprocvars}

uses
  cthreads, Classes, SysUtils, features;

type
  ESentinel = class(Exception);
  EMissed = class(Exception);

var
  Checks: Integer = 0;
  LastClass: TClass;
  LastKind: TNativeErrorKind;
  Source: TStringList;
  FortyTwo: Int64 = 42;

procedure Check(Ok: Boolean; const What: String);
begin
  Inc(Checks);
  if not Ok then
  begin
    WriteLn(StdErr, 'check failed: ', What);
    Halt(1);
  end;
end;

procedure Missed(const Pattern: String);
begin
  raise EMissed.Create('expected an error containing ' + Pattern + ', got none');
end;

// An exception the code before raised: it must mention Pattern.
procedure Got(E: Exception; const Pattern: String);
begin
  Inc(Checks);
  if (E is EMissed) or (Pos(Pattern, E.Message) = 0) then
  begin
    WriteLn(StdErr, 'expected an error containing ', Pattern, ', got: ', E.ClassName, ': ', E.Message);
    Halt(1);
  end;
  LastClass := E.ClassType;
  LastKind := TNativeErrorKind.Native;
  if E is ENativeError then LastKind := ENativeError(E).Kind;
end;

// The doc comment right above the first line containing Declaration.
function Doc(const Declaration: String): String;
var
  At, I: Integer;
  Line: String;
begin
  At := -1;
  for I := 0 to Source.Count - 1 do
    if (Pos(Declaration, Source[I]) > 0) and (Pos('///', Source[I]) = 0) then
    begin
      At := I;
      Break;
    end;
  if At < 0 then
  begin
    WriteLn(StdErr, 'not declared: ', Declaration);
    Halt(1);
  end;
  Result := '';
  I := At - 1;
  while (I >= 0) and (Pos('///', Trim(Source[I])) = 1) do
  begin
    Line := Trim(Source[I]);
    Delete(Line, 1, 3);
    Result := Trim(Line) + LineEnding + Result;
    Dec(I);
  end;
end;

function Has(const Text, Part: String): Boolean;
begin
  Result := Pos(Part, Text) > 0;
end;

// ---------------------------------------------------------------- Pascal subclasses
type
  TTriangle = class(TShape)
  public
    Base, Height: Double;
    constructor Create(ABase, AHeight: Double);
    function Area: Double; override;
    function Name: String; override;
  end;
  TIncomplete = class(TShape);
  TFailing = class(TShape)
  public
    function Area: Double; override;
  end;
  TLoud = class(TCircle)
  public
    function Name: String; override;
  end;
  TNoisy = class(TCircle)
  public
    function Name: String; override;
  end;
  TLouder = class(TNoisy);
  TUnitShape = class(TShape)
  public
    Tag: String;
    constructor Create(const ATag: String);
    function Area: Double; override;
  end;
  TSummer = class(TVisitor)
  public
    Seen: TArrayOfDouble;
    function Visit(AValue: Double): Double; override;
    procedure Finished(const A1: TArrayOfDouble); override;
  end;

constructor TTriangle.Create(ABase, AHeight: Double);
begin
  inherited Create;
  Base := ABase;
  Height := AHeight;
end;

function TTriangle.Area: Double;
begin
  Result := Base * Height / 2;
end;

function TTriangle.Name: String;
begin
  Result := 'triangle';
end;

function TFailing.Area: Double;
begin
  raise ESentinel.Create('from Pascal');
  Result := 0;
end;

function TLoud.Name: String;
begin
  Result := UpperCase(inherited Name);
end;

function TNoisy.Name: String;
begin
  Result := LowerCase(inherited Name) + '!';
end;

constructor TUnitShape.Create(const ATag: String);
begin
  inherited Create;
  Tag := ATag;
end;

function TUnitShape.Area: Double;
begin
  Result := 1;
end;

function TSummer.Visit(AValue: Double): Double;
begin
  Result := AValue * 10;
end;

procedure TSummer.Finished(const A1: TArrayOfDouble);
begin
  Seen := Copy(A1);
end;

// ---------------------------------------------------------------- checks
procedure TestClasses;
var
  C1, Transferred: TCircle;
  S: TSquare;
  Made, Shared, Shape: TShape;
  Canvas: TCanvas;
begin
  C1 := TCircle.Create(2);
  Check((C1.Area = 12) and (C1.Name = 'circle') and (C1.TwiceArea = 24) and (C1.Radius = 2), 'circle methods');
  Check(C1.ToString = 'TCircle', 'ToString names the class');
  Shape := C1;
  Check(Shape is TCircle, 'class hierarchy');
  try
    TCircle.Create(-1).Free;
    Missed('negative radius');
  except
    on E: Exception do Got(E, 'negative radius');
  end;
  Check(LastKind = TNativeErrorKind.InvalidArgument, 'exception kind');
  S := TSquare.Create;
  Check((S.Side = 1) and (S.Area = 1), 'default constructor');
  S.Side := 3;
  Check(S.Area = 9, 'property set');
  with TSquare.Create(2) do
  begin
    Check(Area = 4, 'overloaded constructor');
    Free;
  end;
  Check((AreaOf(S) = 9) and (Describe(C1) = 'circle:0'), 'functions on classes');
  // Enums.
  Check((C1.Kind = TKind.Circle) and (S.Kind = TKind.Square), 'enum results');
  Check(Filled(TFill.Solid) and not Filled(TFill.None), 'enum arguments');
  Check((TKind.Custom.ToString = 'Custom') and (TKind(FortyTwo).ToString = '42') and (Ord(TKind.Custom) = 7), 'enum values');
  Check((TShape.SidesUnknown = -1) and (Abs(Golden - 1.618) < 1e-12), 'constants');

  // Most-derived results, ownership transfer and shared ownership.
  Made := MakeShape(TKind.Circle, 1);
  Check((Made is TCircle) and (TCircle(Made).Radius = 1), 'most-derived result');
  Check(Made.Cast(TSquare) = nil, 'cast to another class fails');
  Check(Made.Cast(TShape) = Made, 'cast to a base is the object');
  Made.Free;
  Canvas := TCanvas.Create;
  Shape := MakeShape(TKind.Square, 2);
  Canvas.Add(Shape);
  Shape.Free;   // C++ took the object: the Pascal object is closed
  Shape := SharedSquare(3);
  Canvas.Share(Shape);
  Shape.Free;
  Transferred := TCircle.Create(1);
  Canvas.Add(Transferred);
  try
    Transferred.Area;
    Missed('closed');
  except
    on E: Exception do Got(E, 'closed');
  end;
  Transferred.Free;
  Check((Canvas.Size = 3) and (Canvas.Total = 4 + 9 + 3), 'ownership transfer');
  Check((Canvas.At(0) is TSquare) and (Canvas.At(0).Area = 4), 'borrowed element');
  Shared := SharedSquare(2);
  Canvas.Share(Shared);
  Check((Shared.Area = 4) and (Canvas.Total = 20), 'shared ownership');
  Transferred := TCircle.Create(1);
  Check(TotalArea(TArrayOfShape.Create(Transferred, Shared)) = 7, 'array of objects');
  Transferred.Free;
  Shared.Free;
  Check(Canvas.Total = 20, 'C++ keeps a shared object after Pascal frees it');
  Canvas.Free;
  S.Free;
  C1.Free;
end;

function Twice(A: Double): Double;
begin
  Result := A * 2;
end;

function PlusOne(A: Double): Double;
begin
  Result := A + 1;
end;

var
  ThreadResult: Double = 0;

type
  TCaller = class(TThread)
  protected
    procedure Execute; override;
  end;

procedure TCaller.Execute;
begin
  ThreadResult := Apply(@PlusOne, 1);
end;

procedure TestValues;
var
  Words: TMapOfStringToInt32;
  Pair: TPairOfDoubleAndDouble;
  Tr: TArrayOfArrayOfDouble;
  Cs: TArrayOfCircle;
  Values: TArrayOfDouble;
  Bytes: TBytes;
  C: TCircle;
  Plus: TNativeInt32ToInt32;
  Calls: Integer;
  Caller: TCaller;
  I: Integer;
  Found: Boolean;

  function Counting(A: Double): Double;
  begin
    Inc(Calls);
    Result := A;
  end;

  function Failing(A: Double): Double;
  begin
    raise ESentinel.Create('from Pascal');
    Result := A;
  end;

  function ViaNative(A: Double): Double;
  var
    F: TNativeInt32ToInt32;
  begin
    F := Adder(1);
    try
      Result := F.Invoke(Trunc(A));
    finally
      F.Free;
    end;
  end;

begin
  // Overloads: Pascal overloads, resolved again in C++.
  Check((Pick(1) = 'int') and (Pick(1.5) = 'double') and (Pick(2.0) = 'double') and (Pick('x') = 'string'), 'scalar overloads');
  C := TCircle.Create(1);
  Check((Pick(TArrayOfDouble.Create(1, 2)) = 'vector') and (Pick(C) = 'shape'), 'container and class overloads');
  C.Free;
  Check((Add(1) = 111) and (Add(1, 2) = 103) and (Add(1, 2, 3) = 6), 'default arguments');

  // Containers.
  Words := CountWords(TArrayOfString.Create('a', 'b', 'a'));
  Found := False;
  for I := 0 to High(Words) do
    if (Words[I].Key = 'a') and (Words[I].Value = 2) then Found := True;
  Check((Length(Words) = 2) and Found, 'map result');
  Pair := MinMax(TArrayOfDouble.Create(3, 1, 2));
  Check((Pair.First = 1) and (Pair.Second = 3), 'pair result');
  try
    MinMax(nil);
    Missed('empty input');
  except
    on E: Exception do Got(E, 'empty input');
  end;
  Check(LastKind = TNativeErrorKind.OutOfRange, 'out_of_range kind');
  Check((Find(TArrayOfInt32.Create(5, 7), 7).Value = 1) and Find(TArrayOfInt32.Create(5, 7), 7).HasValue and
        not Find(TArrayOfInt32.Create(5, 7), 9).HasValue, 'optional result');
  Tr := Transpose(TArrayOfArrayOfDouble.Create(TArrayOfDouble.Create(1, 2, 3), TArrayOfDouble.Create(4, 5, 6)));
  Check((Length(Tr) = 3) and (Tr[0][0] = 1) and (Tr[0][1] = 4) and (Tr[2][1] = 6), 'nested arrays');
  Bytes := BytesOf('AZ');
  Check((ByteSum(TBytes.Create(1, 2, 250)) = 253) and (Length(Bytes) = 2) and (Bytes[0] = 65) and (Bytes[1] = 90), 'bytes');
  Cs := Circles(3);
  Check((Length(Cs) = 3) and (Cs[2].Radius = 3), 'array of class results');
  for C in Cs do C.Free;
  Check((&Repeat('z', 3) = 'zzz') and (Initial('hello') = 'h'), 'characters');
  // Non-const reference parameters are var parameters, written back.
  Values := TArrayOfDouble.Create(1, 2);
  Scale(Values, 3);
  Check((Length(Values) = 2) and (Values[0] = 3) and (Values[1] = 6), 'var argument written back');
  try
    &Repeat('z', -1);
    Missed('negative count');
  except
    on E: Exception do Got(E, 'negative count');
  end;
  Check(Defaults = 'x*1-1.500000', 'all defaults');
  Check(Defaults(TKind.Circle, 'y', True) = 'y*0!-1.500000', 'some defaults');
  Check(Has(Doc('function Defaults(AKind: TKind; const ALabel: String; ALoud: Boolean): String'), '<c>AWeight = 0.5</c>'), 'omitted defaults named');

  // Routines both ways.
  Check(Apply(@Twice, 21) = 42, 'routine');
  try
    Apply(@Failing, 1);
    Missed('from Pascal');
  except
    on E: Exception do Got(E, 'from Pascal');
  end;
  Check(LastClass = ESentinel, 'a routine''s exception comes back unchanged');
  Plus := Adder(40);
  Check(Plus.Invoke(2) = 42, 'C++ std::function as a Pascal object');
  Plus.Free;
  Check(Apply(@ViaNative, 3) = 4, 'C++ function inside a routine');
  Calls := 0;
  Check((Apply(@Counting, 5) = 5) and (Calls = 1), 'nested routine');
  Caller := TCaller.Create(False);
  Caller.WaitFor;
  Caller.Free;
  Check(ThreadResult = 2, 'routine from another thread');
end;

procedure TestObjects;
var
  V, W, R, Chained: TVec2;
  Seg: TSegment;
  Start: TVec2;
  Ledger: TLedger;
  Copy_: TStamp;
  Aim: TAim;
  Stack: TIntStack;
  Items: TArrayOfInt32;
  X: TCircle;
  Tmp: TShape;
  I: Integer;
begin
  // Value type operators, chaining and borrowed references.
  V := TVec2.Create(1, 2);
  W := TVec2.Create(3, 4);
  R := V.Add(W);
  Check((R.X = 4) and (R.Y = 6), 'operator method');
  R.Free;
  R := V.Multiply(2);
  Check(R.Y = 4, 'multiply');
  R.Free;
  R := V.Negate;
  Check(R.X = -1, 'negate');
  R.Free;
  R := V.Subtract(W);
  Check(R.X = -2, 'subtract');
  R.Free;
  R := TVec2.Create(1, 2);
  Check(V.IsEqual(R) and not V.IsEqual(W), 'equality operator');
  R.Free;
  Check((V[1] = 2) and (V.At(1) = 2), 'default array property and index operator');
  R := TVec2.Create(1, 1);
  Check(V.Inner(R) = 3, 'rename');
  R.Free;
  try
    V[2];
    Missed('Vec2 index');
  except
    on E: Exception do Got(E, 'Vec2 index');
  end;
  Chained := V.SetX(10).SetY(20);
  Check((V.X = 10) and (V.Y = 20) and (Chained = V), 'chaining returns the same Pascal object');
  Check((V.AddAssign(W) = V) and (V.X = 13), 'compound assignment');
  V.X := 5;
  Check(V.X = 5, 'property setter');
  R := V.Clone;
  Check(R.Y = 24, 'C++ clone');
  R.Free;
  Check(Sum(TArrayOfVec2.Create(V, W)) = 5 + 24 + 3 + 4, 'array of values');
  V.Free;
  W.Free;

  Seg := TSegment.Create;
  W := TVec2.Create(3, 4);
  Seg.&End := W;
  W.Free;
  Check(Seg.Length2 = 25, 'object property set');
  Start := Seg.First;
  Start.X := 3;
  Check((Seg.Start.X = 3) and (Seg.Length2 = 16), 'borrowed reference writes through');
  Seg.Start.Y := 4;
  Check(Seg.Length2 = 0, 'object property borrowed');
  Check(Seg.Start = Seg.Start, 'a member object is one Pascal object');
  Seg.Free;   // and the objects it owns

  // Templates, extensions, qualified names and copy types.
  Stack := TIntStack.Create;
  Stack.Push(1);
  Stack.Push(2);
  Items := Stack.Items;
  Check((Stack.Size = 2) and (Length(Items) = 2) and (Items[1] = 2) and (Stack.Pop = 2), 'template instance');
  Stack.Free;
  Check((ClampInt(42) = 10) and (ClampInt(3, 5) = 3) and (ClampReal(2.5) = 2.5), 'function templates');
  X := TCircle.Create(2);
  Check((X.&Label = '#circle') and (X.&Label('@') = '@circle') and (X.Diameter = 4), 'extensions');
  X.Free;
  X := TCircle.&Unit;
  Check(X.Radius = 1, 'static extension');
  X.Free;
  with TFeaturesLegacyTag.Create do
  begin
    Check((FeaturesLegacyVersion = 2) and (Id = 7), 'qualified names');
    Free;
  end;
  Check(TFeaturesLegacyMode.Off.ToString = 'Off', 'qualified enum');
  Ledger := TLedger.Create;
  Copy_ := Ledger.Latest;
  Copy_.Value := 5;
  Check(Ledger.Stamp.Value = 1, 'copy type');
  Copy_.Free;
  Ledger.Free;
  Aim := TAim.Create;
  Aim.Origin.X := 2;
  Check(Aim.Target.Point.X = 2, 'nested borrowed properties');
  Aim.Free;
  Check(Has(Source.Text, 'property Point: TVec2 read _Get') and not Has(Doc('property Point: TVec2'), 'write'), 'read-only property');

  // Errors, closing and releasing.
  X := TCircle.Create(1);
  Check((X.Area = 3) and not X.IsClosed, 'before close');
  X.Close;
  Check(X.IsClosed and (X.ToString = 'TCircle (closed)'), 'closed');
  try
    X.Area;
    Missed('closed');
  except
    on E: Exception do Got(E, 'closed');
  end;
  Check(LastKind = TNativeErrorKind.Binding, 'binding error kind');
  X.Close;
  X.Free;
  for I := 1 to 200 do
  begin
    Tmp := MakeShape(TKind.Square, I);
    Check(Tmp.Area = I * I, 'many objects');
    if I mod 2 = 0 then Tmp.Close;
    Tmp.Free;
  end;
end;

procedure TestSubclasses;
var
  T: TTriangle;
  C, Q: TShape;
  L: TLoud;
  Canvas: TCanvas;
  U: TUnitShape;
  Back: TShape;
  Sm: TSummer;
  V: TVisitor;
begin
  T := TTriangle.Create(4, 3);
  Check((T.Area = 6) and (AreaOf(T) = 6) and (T.TwiceArea = 12) and (Describe(T) = 'triangle:7'), 'override called from C++ and from Pascal');
  C := TCircle.Create(1);
  Q := TSquare.Create(2);
  Check((TotalArea(TArrayOfShape.Create(T, C, Q)) = 13) and (T.&Label('@') = '@triangle'), 'mixed overrides');
  C.Free;
  Q.Free;
  T.Free;
  C := TIncomplete.Create;
  try
    AreaOf(C);
    Missed('pure virtual');
  except
    on E: Exception do Got(E, 'pure virtual');
  end;
  C.Free;
  C := TFailing.Create;
  try
    AreaOf(C);
    Missed('from Pascal');
  except
    on E: Exception do Got(E, 'from Pascal');
  end;
  C.Free;
  Check(LastClass = ESentinel, 'an override''s exception comes back unchanged');
  try
    TShape.Create.Free;
    Missed('abstract');
  except
    on E: Exception do Got(E, 'abstract');
  end;
  Check(LastKind = TNativeErrorKind.Binding, 'constructing an abstract class');
  L := TLoud.Create(2);
  Check((Describe(L) = 'CIRCLE:0') and (L.Name = 'CIRCLE') and (L.Area = 12), 'inherited runs the C++ method');
  L.Free;
  C := TLouder.Create(1);
  Check(Describe(C) = 'circle!:0', 'inherited override');
  C.Free;
  Canvas := TCanvas.Create;
  U := TUnitShape.Create('kept');
  Canvas.Add(U);
  Check(U.Area = 1, 'Pascal object owned by C++');
  Q := TSquare.Create(2);
  Canvas.Share(Q);
  Q.Free;
  Check((Canvas.Total = 5) and (Canvas.Size = 2), 'C++ calls the Pascal object it owns');
  Back := Canvas.At(0);
  Check((Back = U) and ((Back as TUnitShape).Tag = 'kept'), 'the same Pascal object comes back');
  Canvas.Clear;
  U.Free;
  U := TUnitShape.Create('');
  Canvas.Share(U);
  Check(Canvas.Total = 1, 'shared Pascal object');
  Canvas.Clear;
  U.Free;
  Canvas.Free;
  Sm := TSummer.Create;
  Check(RunVisitor(Sm, TArrayOfDouble.Create(1, 2)) = 30, 'callback interface');
  Check((Length(Sm.Seen) = 2) and (Sm.Seen[1] = 2), 'subclass state');
  Check(Sm.Visit(4) = 40, 'a direct call runs the override');
  Sm.Free;
  V := TVisitor.Create;
  Check(RunVisitor(V, TArrayOfDouble.Create(1, 2)) = 3, 'C++ default implementation');
  V.Free;
end;

procedure TestDocs;
var
  D: String;
  U: TVec2;
begin
  D := Doc('function Add(AA: Int32): Int32;');
  Check(Has(D, 'Adds three integers, Socha &amp; Dorigo style.'), 'summary');
  Check(Has(D, '<param name="AA">first term</param>'), 'parameters');
  Check(Has(D, '<returns>the sum</returns>'), 'returns');
  Check(Has(D, '<para><b>Example</b></para>') and Has(D, '<code>') and Has(D, 'int total = add(1);'), 'heading and code block');
  Check(Has(D, 'See also: <c>pick</c>'), 'see also');
  Check(Has(D, '<exception cref="ENativeError">Kind <c>OutOfRange</c> (C++ <c>std::out_of_range</c>)'), 'errors');
  Check(Has(D, '<c>AB = 10</c>, <c>AC = 100</c>'), 'omitted defaults');
  Check(Has(D, '<list type="bullet">'), 'lists');
  Check(Has(Doc('constructor Create(ARadius: Double); overload;'), 'Kind <c>InvalidArgument</c>'), 'constructor errors');
  Check(Has(Doc('property X: Double'), 'Horizontal component.'), 'property documentation');
  Check(Has(Doc('TShape = class(TNativeObject)'), 'Abstract in C++: subclass it in Pascal'), 'class remarks');
  Check(Has(Doc('function Area: Double; overload; virtual;'), 'Pure virtual in C++'), 'pure virtual methods marked');

  // Policy modules: features::data functions are class functions of Data.
  U := Data.&Unit;
  Check((U.X = 1) and (U.Y = 0), 'submodule result');
  U.Free;
  Check((Data.Scaled(3) = 6) and (Data.Scaled(2.5) = 6.25), 'submodule overloads');
  Check((Data.&Repeat('ab') = 'abab') and (&Repeat('x', 3) = 'xxx'), 'submodule names are separate');
  Check(Has(Doc('class function &Unit: TVec2; static; overload;'), 'Mapped by the policy'), 'submodule docs');
end;

begin
  Source := TStringList.Create;
  Source.LoadFromFile(ParamStr(1));
  TestClasses;
  TestValues;
  TestObjects;
  TestSubclasses;
  TestDocs;
  Source.Free;
  WriteLn('Pascal API passed (', Checks, ' checks)');
end.
