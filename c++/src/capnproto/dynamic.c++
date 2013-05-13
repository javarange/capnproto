// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#define CAPNPROTO_PRIVATE
#include "dynamic.h"
#include "logging.h"

namespace capnproto {

namespace {

template <typename T, typename U>
CAPNPROTO_ALWAYS_INLINE(T bitCast(U value));

template <typename T, typename U>
inline T bitCast(U value) {
  static_assert(sizeof(T) == sizeof(U), "Size must match.");
  return value;
}
template <>
inline float bitCast<float, uint32_t>(uint32_t value) {
  float result;
  memcpy(&result, &value, sizeof(value));
  return result;
}
template <>
inline double bitCast<double, uint64_t>(uint64_t value) {
  double result;
  memcpy(&result, &value, sizeof(value));
  return result;
}
template <>
inline uint32_t bitCast<uint32_t, float>(float value) {
  uint32_t result;
  memcpy(&result, &value, sizeof(value));
  return result;
}
template <>
inline uint64_t bitCast<uint64_t, double>(double value) {
  uint64_t result;
  memcpy(&result, &value, sizeof(value));
  return result;
}

internal::FieldSize elementSizeFor(schema::Type::Body::Which elementType) {
  switch (elementType) {
    case schema::Type::Body::VOID_TYPE: return internal::FieldSize::VOID;
    case schema::Type::Body::BOOL_TYPE: return internal::FieldSize::BIT;
    case schema::Type::Body::INT8_TYPE: return internal::FieldSize::BYTE;
    case schema::Type::Body::INT16_TYPE: return internal::FieldSize::TWO_BYTES;
    case schema::Type::Body::INT32_TYPE: return internal::FieldSize::FOUR_BYTES;
    case schema::Type::Body::INT64_TYPE: return internal::FieldSize::EIGHT_BYTES;
    case schema::Type::Body::UINT8_TYPE: return internal::FieldSize::BYTE;
    case schema::Type::Body::UINT16_TYPE: return internal::FieldSize::TWO_BYTES;
    case schema::Type::Body::UINT32_TYPE: return internal::FieldSize::FOUR_BYTES;
    case schema::Type::Body::UINT64_TYPE: return internal::FieldSize::EIGHT_BYTES;
    case schema::Type::Body::FLOAT32_TYPE: return internal::FieldSize::FOUR_BYTES;
    case schema::Type::Body::FLOAT64_TYPE: return internal::FieldSize::EIGHT_BYTES;

    case schema::Type::Body::TEXT_TYPE: return internal::FieldSize::REFERENCE;
    case schema::Type::Body::DATA_TYPE: return internal::FieldSize::REFERENCE;
    case schema::Type::Body::LIST_TYPE: return internal::FieldSize::REFERENCE;
    case schema::Type::Body::ENUM_TYPE: return internal::FieldSize::TWO_BYTES;
    case schema::Type::Body::STRUCT_TYPE: return internal::FieldSize::INLINE_COMPOSITE;
    case schema::Type::Body::INTERFACE_TYPE: return internal::FieldSize::REFERENCE;
    case schema::Type::Body::OBJECT_TYPE: FAIL_CHECK("List(Object) not supported."); break;
  }
  FAIL_CHECK("Can't get here.");
  return internal::FieldSize::VOID;
}

inline internal::StructSize structSizeFromSchema(StructSchema schema) {
  auto node = schema.getProto().getBody().getStructNode();
  return internal::StructSize(
      node.getDataSectionWordSize() * WORDS,
      node.getPointerSectionSize() * REFERENCES,
      static_cast<internal::FieldSize>(node.getPreferredListEncoding()));
}

}  // namespace

// =======================================================================================

Maybe<EnumSchema::Enumerant> DynamicEnum::getEnumerant() {
  auto enumerants = schema.getEnumerants();
  if (value < enumerants.size()) {
    return enumerants[value];
  } else {
    return nullptr;
  }
}

uint16_t DynamicEnum::asImpl(uint64_t requestedTypeId) {
  VALIDATE_INPUT(requestedTypeId == schema.getProto().getId(),
                 "Type mismatch in DynamicEnum.as().") {
    // Go on with value.
  }
  return value;
}

// =======================================================================================

DynamicStruct::Reader DynamicObject::as(StructSchema schema) {
  if (reader.kind == internal::ObjectKind::NULL_POINTER) {
    return DynamicStruct::Reader(schema, internal::StructReader());
  }
  VALIDATE_INPUT(reader.kind == internal::ObjectKind::STRUCT, "Object is not a struct.") {
    return DynamicStruct::Reader(schema, internal::StructReader());
  }
  return DynamicStruct::Reader(schema, reader.structReader);
}

DynamicList::Reader DynamicObject::as(ListSchema schema) {
  if (reader.kind == internal::ObjectKind::NULL_POINTER) {
    return DynamicList::Reader(schema, internal::ListReader());
  }
  VALIDATE_INPUT(reader.kind == internal::ObjectKind::LIST, "Object is not a list.") {
    return DynamicList::Reader(schema, internal::ListReader());
  }
  return DynamicList::Reader(schema, reader.listReader);
}

// =======================================================================================

Maybe<StructSchema::Member> DynamicUnion::Reader::which() {
  auto members = schema.getMembers();
  uint16_t discrim = reader.getDataField<uint16_t>(
      schema.getProto().getBody().getUnionMember().getDiscriminantOffset() * ELEMENTS);

  if (discrim < members.size()) {
    return members[discrim];
  } else {
    return nullptr;
  }
}
Maybe<StructSchema::Member> DynamicUnion::Builder::which() {
  auto members = schema.getMembers();
  uint16_t discrim = builder.getDataField<uint16_t>(
      schema.getProto().getBody().getUnionMember().getDiscriminantOffset() * ELEMENTS);

  if (discrim < members.size()) {
    return members[discrim];
  } else {
    return nullptr;
  }
}

DynamicValue::Reader DynamicUnion::Reader::get() {
  return DynamicValue::Reader(DynamicStruct::Reader::getImpl(reader, checkIsKnown()));
}

DynamicValue::Builder DynamicUnion::Builder::get() {
  return DynamicValue::Builder(DynamicStruct::Builder::getImpl(builder, checkIsKnown()));
}

void DynamicUnion::Builder::set(StructSchema::Member member, DynamicValue::Reader value) {
  setDiscriminant(member);
  DynamicStruct::Builder::setImpl(builder, member, value);
}

DynamicValue::Builder DynamicUnion::Builder::init(StructSchema::Member member) {
  setDiscriminant(member);
  return DynamicStruct::Builder::initImpl(builder, member);
}

DynamicValue::Builder DynamicUnion::Builder::init(StructSchema::Member member, uint size) {
  setDiscriminant(member);
  return DynamicStruct::Builder::initImpl(builder, member, size);
}

DynamicStruct::Builder DynamicUnion::Builder::getObject(StructSchema schema) {
  return DynamicStruct::Builder::getObjectImpl(builder, checkIsObject(), schema);
}
DynamicList::Builder DynamicUnion::Builder::getObject(ListSchema schema) {
  return DynamicStruct::Builder::getObjectImpl(builder, checkIsObject(), schema);
}
Text::Builder DynamicUnion::Builder::getObjectAsText() {
  return DynamicStruct::Builder::getObjectAsTextImpl(builder, checkIsObject());
}
Data::Builder DynamicUnion::Builder::getObjectAsData() {
  return DynamicStruct::Builder::getObjectAsDataImpl(builder, checkIsObject());
}
DynamicStruct::Builder DynamicUnion::Builder::initObject(
    StructSchema::Member member, StructSchema type) {
  setObjectDiscriminant(member);
  return DynamicStruct::Builder::initFieldImpl(builder, member, type);
}
DynamicList::Builder DynamicUnion::Builder::initObject(
    StructSchema::Member member, ListSchema type, uint size) {
  setObjectDiscriminant(member);
  return DynamicStruct::Builder::initFieldImpl(builder, member, type, size);
}
Text::Builder DynamicUnion::Builder::initObjectAsText(StructSchema::Member member, uint size) {
  setObjectDiscriminant(member);
  return DynamicStruct::Builder::initFieldAsTextImpl(builder, member, size);
}
Data::Builder DynamicUnion::Builder::initObjectAsData(StructSchema::Member member, uint size) {
  setObjectDiscriminant(member);
  return DynamicStruct::Builder::initFieldAsDataImpl(builder, member, size);
}

void DynamicUnion::Builder::set(Text::Reader name, DynamicValue::Reader value) {
  set(schema.getMemberByName(name), value);
}
DynamicValue::Builder DynamicUnion::Builder::init(Text::Reader name) {
  return init(schema.getMemberByName(name));
}
DynamicValue::Builder DynamicUnion::Builder::init(Text::Reader name, uint size) {
  return init(schema.getMemberByName(name), size);
}
DynamicStruct::Builder DynamicUnion::Builder::initObject(Text::Reader name, StructSchema type) {
  return initObject(schema.getMemberByName(name), type);
}
DynamicList::Builder DynamicUnion::Builder::initObject(
    Text::Reader name, ListSchema type, uint size) {
  return initObject(schema.getMemberByName(name), type, size);
}
Text::Builder DynamicUnion::Builder::initObjectAsText(Text::Reader name, uint size) {
  return initObjectAsText(schema.getMemberByName(name), size);
}
Data::Builder DynamicUnion::Builder::initObjectAsData(Text::Reader name, uint size) {
  return initObjectAsData(schema.getMemberByName(name), size);
}

StructSchema::Member DynamicUnion::Reader::checkIsKnown() {
  auto w = which();
  PRECOND(w != nullptr, "Can't get() unknown union value.");
  CHECK(w->getProto().getBody().which() == schema::StructNode::Member::Body::FIELD_MEMBER,
        "Unsupported union member type.");
  return *w;
}
StructSchema::Member DynamicUnion::Builder::checkIsKnown() {
  auto w = which();
  PRECOND(w != nullptr, "Can't get() unknown union value.");
  CHECK(w->getProto().getBody().which() == schema::StructNode::Member::Body::FIELD_MEMBER,
        "Unsupported union member type.");
  return *w;
}

StructSchema::Member DynamicUnion::Builder::checkIsObject() {
  auto result = checkIsKnown();
  PRECOND(result.getProto().getBody().getFieldMember().getType().getBody().which() ==
          schema::Type::Body::OBJECT_TYPE, "Expected Object.");
  return result;
}

void DynamicUnion::Builder::setDiscriminant(StructSchema::Member member) {
  auto containingUnion = member.getContainingUnion();
  PRECOND(containingUnion != nullptr && *containingUnion == schema,
          "`member` is not a member of this union.");
  builder.setDataField<uint16_t>(
      schema.getProto().getBody().getUnionMember().getDiscriminantOffset() * ELEMENTS,
      member.getIndex());
}

void DynamicUnion::Builder::setObjectDiscriminant(StructSchema::Member member) {
  PRECOND(member.getProto().getBody().getFieldMember().getType().getBody().which() ==
          schema::Type::Body::OBJECT_TYPE, "Expected Object.");
  setDiscriminant(member);
}

// =======================================================================================

void DynamicStruct::Reader::verifyTypeId(uint64_t id) {
  VALIDATE_INPUT(id == schema.getProto().getId(),
                 "Type mismatch when using DynamicStruct::Reader::as().") {
    // Go on with bad type ID.
  }
}
void DynamicStruct::Builder::verifyTypeId(uint64_t id) {
  VALIDATE_INPUT(id == schema.getProto().getId(),
                 "Type mismatch when using DynamicStruct::Builder::as().") {
    // Go on with bad type ID.
  }
}

DynamicValue::Reader DynamicStruct::Reader::get(StructSchema::Member member) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  return getImpl(reader, member);
}
DynamicValue::Builder DynamicStruct::Builder::get(StructSchema::Member member) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  return getImpl(builder, member);
}
void DynamicStruct::Builder::set(StructSchema::Member member, DynamicValue::Reader value) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  return setImpl(builder, member, value);
}
DynamicValue::Builder DynamicStruct::Builder::init(StructSchema::Member member) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  return initImpl(builder, member);
}
DynamicValue::Builder DynamicStruct::Builder::init(StructSchema::Member member, uint size) {
  PRECOND(member.getContainingStruct() == schema,
          "`member` is not a member of this struct.");
  return initImpl(builder, member, size);
}

DynamicStruct::Builder DynamicStruct::Builder::getObject(
    StructSchema::Member member, StructSchema type) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_PRECOND("Expected an Object.");
      return DynamicStruct::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      PRECOND(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return getObjectImpl(builder, member, type);
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return DynamicStruct::Builder();
}
DynamicList::Builder DynamicStruct::Builder::getObject(
    StructSchema::Member member, ListSchema type) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_PRECOND("Expected an Object.");
      return DynamicList::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      PRECOND(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return getObjectImpl(builder, member, type);
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return DynamicList::Builder();
}
Text::Builder DynamicStruct::Builder::getObjectAsText(StructSchema::Member member) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_PRECOND("Expected an Object.");
      return Text::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      PRECOND(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return getObjectAsDataImpl(builder, member);
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return Text::Builder();
}
Data::Builder DynamicStruct::Builder::getObjectAsData(StructSchema::Member member) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_PRECOND("Expected an Object.");
      return Data::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      PRECOND(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return getObjectAsDataImpl(builder, member);
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return Data::Builder();
}

DynamicStruct::Builder DynamicStruct::Builder::initObject(
    StructSchema::Member member, StructSchema type) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_PRECOND("Expected an Object.");
      return DynamicStruct::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      PRECOND(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return initFieldImpl(builder, member, type);
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return DynamicStruct::Builder();
}
DynamicList::Builder DynamicStruct::Builder::initObject(
    StructSchema::Member member, ListSchema type, uint size) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_PRECOND("Expected an Object.");
      return DynamicList::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      PRECOND(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return initFieldImpl(builder, member, type, size);
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return DynamicList::Builder();
}
Text::Builder DynamicStruct::Builder::initObjectAsText(StructSchema::Member member, uint size) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_PRECOND("Expected an Object.");
      return Text::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      PRECOND(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return initFieldAsDataImpl(builder, member, size);
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return Text::Builder();
}
Data::Builder DynamicStruct::Builder::initObjectAsData(StructSchema::Member member, uint size) {
  PRECOND(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_PRECOND("Expected an Object.");
      return Data::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      PRECOND(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return initFieldAsDataImpl(builder, member, size);
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return Data::Builder();
}

DynamicValue::Reader DynamicStruct::Reader::get(Text::Reader name) {
  return getImpl(reader, schema.getMemberByName(name));
}
DynamicValue::Builder DynamicStruct::Builder::get(Text::Reader name) {
  return getImpl(builder, schema.getMemberByName(name));
}
void DynamicStruct::Builder::set(Text::Reader name, DynamicValue::Reader value) {
  setImpl(builder, schema.getMemberByName(name), value);
}
void DynamicStruct::Builder::set(Text::Reader name,
                                 std::initializer_list<DynamicValue::Reader> value) {
  init(name, value.size()).as<DynamicList>().copyFrom(value);
}
DynamicValue::Builder DynamicStruct::Builder::init(Text::Reader name) {
  return initImpl(builder, schema.getMemberByName(name));
}
DynamicValue::Builder DynamicStruct::Builder::init(Text::Reader name, uint size) {
  return initImpl(builder, schema.getMemberByName(name), size);
}
DynamicStruct::Builder DynamicStruct::Builder::getObject(
    Text::Reader name, StructSchema type) {
  return getObject(schema.getMemberByName(name), type);
}
DynamicList::Builder DynamicStruct::Builder::getObject(Text::Reader name, ListSchema type) {
  return getObject(schema.getMemberByName(name), type);
}
Text::Builder DynamicStruct::Builder::getObjectAsText(Text::Reader name) {
  return getObjectAsText(schema.getMemberByName(name));
}
Data::Builder DynamicStruct::Builder::getObjectAsData(Text::Reader name) {
  return getObjectAsText(schema.getMemberByName(name));
}
DynamicStruct::Builder DynamicStruct::Builder::initObject(
    Text::Reader name, StructSchema type) {
  return initObject(schema.getMemberByName(name), type);
}
DynamicList::Builder DynamicStruct::Builder::initObject(
    Text::Reader name, ListSchema type, uint size) {
  return initObject(schema.getMemberByName(name), type, size);
}
Text::Builder DynamicStruct::Builder::initObjectAsText(Text::Reader name, uint size) {
  return initObjectAsText(schema.getMemberByName(name), size);
}
Data::Builder DynamicStruct::Builder::initObjectAsData(Text::Reader name, uint size) {
  return initObjectAsText(schema.getMemberByName(name), size);
}

void DynamicStruct::Builder::copyFrom(Reader other) {
  // TODO(now): copyFrom on StructBuilder.
  // TODO(now): don't forget to check types match.
  FAIL_CHECK("Unimplemented: copyFrom()");
}

DynamicValue::Reader DynamicStruct::Reader::getImpl(
    internal::StructReader reader, StructSchema::Member member) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      return DynamicUnion::Reader(member.asUnion(), reader);

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      auto type = field.getType().getBody();
      auto dval = field.getDefaultValue().getBody();

      switch (type.which()) {
        case schema::Type::Body::VOID_TYPE:
          return DynamicValue::Reader(reader.getDataField<Void>(field.getOffset() * ELEMENTS));

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::Body::discrim##_TYPE: \
          return DynamicValue::Reader(reader.getDataField<type>( \
              field.getOffset() * ELEMENTS, \
              bitCast<typename internal::MaskType<type>::Type>(dval.get##titleCase##Value())));

        HANDLE_TYPE(BOOL, Bool, bool)
        HANDLE_TYPE(INT8, Int8, int8_t)
        HANDLE_TYPE(INT16, Int16, int16_t)
        HANDLE_TYPE(INT32, Int32, int32_t)
        HANDLE_TYPE(INT64, Int64, int64_t)
        HANDLE_TYPE(UINT8, Uint8, uint8_t)
        HANDLE_TYPE(UINT16, Uint16, uint16_t)
        HANDLE_TYPE(UINT32, Uint32, uint32_t)
        HANDLE_TYPE(UINT64, Uint64, uint64_t)
        HANDLE_TYPE(FLOAT32, Float32, float)
        HANDLE_TYPE(FLOAT64, Float64, double)

#undef HANDLE_TYPE

        case schema::Type::Body::ENUM_TYPE: {
          uint16_t typedDval;
          typedDval = dval.getEnumValue();
          return DynamicValue::Reader(DynamicEnum(
              member.getContainingStruct().getDependency(type.getEnumType()).asEnum(),
              reader.getDataField<uint16_t>(field.getOffset() * ELEMENTS, typedDval)));
        }

        case schema::Type::Body::TEXT_TYPE: {
          Text::Reader typedDval = dval.getTextValue();
          return DynamicValue::Reader(
              reader.getBlobField<Text>(field.getOffset() * REFERENCES,
                                        typedDval.data(), typedDval.size() * BYTES));
        }

        case schema::Type::Body::DATA_TYPE: {
          Data::Reader typedDval = dval.getDataValue();
          return DynamicValue::Reader(
              reader.getBlobField<Data>(field.getOffset() * REFERENCES,
                                        typedDval.data(), typedDval.size() * BYTES));
        }

        case schema::Type::Body::LIST_TYPE: {
          auto elementType = type.getListType();
          return DynamicValue::Reader(DynamicList::Reader(
              ListSchema::of(elementType, member.getContainingStruct()),
              reader.getListField(field.getOffset() * REFERENCES,
                                  elementSizeFor(elementType.getBody().which()),
                                  dval.getListValue<internal::TrustedMessage>())));
        }

        case schema::Type::Body::STRUCT_TYPE: {
          return DynamicValue::Reader(DynamicStruct::Reader(
              member.getContainingStruct().getDependency(type.getStructType()).asStruct(),
              reader.getStructField(field.getOffset() * REFERENCES,
                                    dval.getStructValue<internal::TrustedMessage>())));
        }

        case schema::Type::Body::OBJECT_TYPE: {
          return DynamicValue::Reader(DynamicObject(
              reader.getObjectField(field.getOffset() * REFERENCES,
                                    dval.getObjectValue<internal::TrustedMessage>())));
        }

        case schema::Type::Body::INTERFACE_TYPE:
          FAIL_CHECK("Interfaces not yet implemented.");
          break;
      }

      FAIL_CHECK("switch() missing case.", type.which());
      return DynamicValue::Reader();
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return DynamicValue::Reader();
}

DynamicValue::Builder DynamicStruct::Builder::getImpl(
    internal::StructBuilder builder, StructSchema::Member member) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      return DynamicUnion::Builder(member.asUnion(), builder);

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      auto type = field.getType().getBody();
      auto dval = field.getDefaultValue().getBody();

      switch (type.which()) {
        case schema::Type::Body::VOID_TYPE:
          return DynamicValue::Builder(builder.getDataField<Void>(field.getOffset() * ELEMENTS));

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::Body::discrim##_TYPE: \
          return DynamicValue::Builder(builder.getDataField<type>( \
              field.getOffset() * ELEMENTS, \
              bitCast<typename internal::MaskType<type>::Type>(dval.get##titleCase##Value())));

        HANDLE_TYPE(BOOL, Bool, bool)
        HANDLE_TYPE(INT8, Int8, int8_t)
        HANDLE_TYPE(INT16, Int16, int16_t)
        HANDLE_TYPE(INT32, Int32, int32_t)
        HANDLE_TYPE(INT64, Int64, int64_t)
        HANDLE_TYPE(UINT8, Uint8, uint8_t)
        HANDLE_TYPE(UINT16, Uint16, uint16_t)
        HANDLE_TYPE(UINT32, Uint32, uint32_t)
        HANDLE_TYPE(UINT64, Uint64, uint64_t)
        HANDLE_TYPE(FLOAT32, Float32, float)
        HANDLE_TYPE(FLOAT64, Float64, double)

#undef HANDLE_TYPE

        case schema::Type::Body::ENUM_TYPE: {
          uint16_t typedDval;
          typedDval = dval.getEnumValue();
          return DynamicValue::Builder(DynamicEnum(
              member.getContainingStruct().getDependency(type.getEnumType()).asEnum(),
              builder.getDataField<uint16_t>(field.getOffset() * ELEMENTS, typedDval)));
        }

        case schema::Type::Body::TEXT_TYPE: {
          Text::Reader typedDval = dval.getTextValue();
          return DynamicValue::Builder(
              builder.getBlobField<Text>(field.getOffset() * REFERENCES,
                                         typedDval.data(), typedDval.size() * BYTES));
        }

        case schema::Type::Body::DATA_TYPE: {
          Data::Reader typedDval = dval.getDataValue();
          return DynamicValue::Builder(
              builder.getBlobField<Data>(field.getOffset() * REFERENCES,
                                         typedDval.data(), typedDval.size() * BYTES));
        }

        case schema::Type::Body::LIST_TYPE:
          return DynamicValue::Builder(DynamicList::Builder(
              ListSchema::of(type.getListType(), member.getContainingStruct()),
              builder.getListField(field.getOffset() * REFERENCES,
                                   dval.getListValue<internal::TrustedMessage>())));

        case schema::Type::Body::STRUCT_TYPE: {
          auto structSchema =
              member.getContainingStruct().getDependency(type.getStructType()).asStruct();
          return DynamicValue::Builder(DynamicStruct::Builder(
              structSchema,
              builder.getStructField(
                  field.getOffset() * REFERENCES,
                  structSizeFromSchema(structSchema),
                  dval.getStructValue<internal::TrustedMessage>())));
        }

        case schema::Type::Body::OBJECT_TYPE: {
          return DynamicValue::Builder(DynamicObject(
              builder.asReader().getObjectField(
                  field.getOffset() * REFERENCES,
                  dval.getObjectValue<internal::TrustedMessage>())));
        }

        case schema::Type::Body::INTERFACE_TYPE:
          FAIL_CHECK("Interfaces not yet implemented.");
          break;
      }

      FAIL_CHECK("switch() missing case.", type.which());
      return DynamicValue::Builder();
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
  return DynamicValue::Builder();
}
DynamicStruct::Builder DynamicStruct::Builder::getObjectImpl(
    internal::StructBuilder builder, StructSchema::Member field, StructSchema type) {
  return DynamicStruct::Builder(type,
      builder.getStructField(
          field.getProto().getBody().getFieldMember().getOffset() * REFERENCES,
          structSizeFromSchema(type), nullptr));
}
DynamicList::Builder DynamicStruct::Builder::getObjectImpl(
    internal::StructBuilder builder, StructSchema::Member field, ListSchema type) {
  return DynamicList::Builder(type,
      builder.getListField(
          field.getProto().getBody().getFieldMember().getOffset() * REFERENCES,
          nullptr));
}
Text::Builder DynamicStruct::Builder::getObjectAsTextImpl(
    internal::StructBuilder builder, StructSchema::Member field) {
  return builder.getBlobField<Text>(
      field.getProto().getBody().getFieldMember().getOffset() * REFERENCES, nullptr, 0 * BYTES);
}
Data::Builder DynamicStruct::Builder::getObjectAsDataImpl(
    internal::StructBuilder builder, StructSchema::Member field) {
  return builder.getBlobField<Data>(
      field.getProto().getBody().getFieldMember().getOffset() * REFERENCES, nullptr, 0 * BYTES);
}

void DynamicStruct::Builder::setImpl(
    internal::StructBuilder builder, StructSchema::Member member, DynamicValue::Reader value) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER: {
      auto src = value.as<DynamicUnion>();
      auto which = src.which();
      VALIDATE_INPUT(which != nullptr,
          "Trying to copy a union value, but the union's discriminant is not recognized.  It "
          "was probably constructed using a newer version of the schema.") {
        return;
      }

      getImpl(builder, member).as<DynamicUnion>().set(member, src.get());
      return;
    }

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      auto type = field.getType().getBody();
      auto dval = field.getDefaultValue().getBody();

      switch (type.which()) {
        case schema::Type::Body::VOID_TYPE:
          builder.setDataField<Void>(field.getOffset() * ELEMENTS, value.as<Void>());
          return;

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::Body::discrim##_TYPE: \
          builder.setDataField<type>( \
              field.getOffset() * ELEMENTS, value.as<type>(), \
              bitCast<internal::Mask<type> >(dval.get##titleCase##Value())); \
          return;

        HANDLE_TYPE(BOOL, Bool, bool)
        HANDLE_TYPE(INT8, Int8, int8_t)
        HANDLE_TYPE(INT16, Int16, int16_t)
        HANDLE_TYPE(INT32, Int32, int32_t)
        HANDLE_TYPE(INT64, Int64, int64_t)
        HANDLE_TYPE(UINT8, Uint8, uint8_t)
        HANDLE_TYPE(UINT16, Uint16, uint16_t)
        HANDLE_TYPE(UINT32, Uint32, uint32_t)
        HANDLE_TYPE(UINT64, Uint64, uint64_t)
        HANDLE_TYPE(FLOAT32, Float32, float)
        HANDLE_TYPE(FLOAT64, Float64, double)

#undef HANDLE_TYPE

        case schema::Type::Body::ENUM_TYPE:
          builder.setDataField<uint16_t>(
              field.getOffset() * ELEMENTS, value.as<DynamicEnum>().getRaw(),
              dval.getEnumValue());
          return;

        case schema::Type::Body::TEXT_TYPE:
          builder.setBlobField<Text>(field.getOffset() * REFERENCES, value.as<Text>());
          return;

        case schema::Type::Body::DATA_TYPE:
          builder.setBlobField<Data>(field.getOffset() * REFERENCES, value.as<Data>());
          return;

        case schema::Type::Body::LIST_TYPE: {
          // TODO(now):  We need to do a schemaless copy to avoid losing information if the values
          //   are larger than what the schema defines.
          auto listValue = value.as<DynamicList>();
          initImpl(builder, member, listValue.size())
              .as<DynamicList>().copyFrom(listValue);
          return;
        }

        case schema::Type::Body::STRUCT_TYPE: {
          // TODO(now):  We need to do a schemaless copy to avoid losing information if the values
          //   are larger than what the schema defines.
          initImpl(builder, member).as<DynamicStruct>()
              .copyFrom(value.as<DynamicStruct>());
          return;
        }

        case schema::Type::Body::OBJECT_TYPE: {
          // TODO(now):  Perform schemaless copy.
          FAIL_CHECK("TODO");
          return;
        }

        case schema::Type::Body::INTERFACE_TYPE:
          FAIL_CHECK("Interfaces not yet implemented.");
          return;
      }

      FAIL_CHECK("switch() missing case.", type.which());
      return;
    }
  }

  FAIL_CHECK("switch() missing case.", member.getProto().getBody().which());
}

DynamicValue::Builder DynamicStruct::Builder::initImpl(
    internal::StructBuilder builder, StructSchema::Member member, uint size) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_VALIDATE_INPUT(
          "Can't init() a union.  get() it first and then init() one of its members.");
      break;

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto type = member.getProto().getBody().getFieldMember().getType().getBody();
      switch (type.which()) {
        case schema::Type::Body::LIST_TYPE:
          return initFieldImpl(builder, member, ListSchema::of(
              type.getListType(), member.getContainingStruct()), size);
        case schema::Type::Body::TEXT_TYPE:
          return initFieldAsTextImpl(builder, member, size);
        case schema::Type::Body::DATA_TYPE:
          return initFieldAsDataImpl(builder, member, size);
        default:
          FAIL_VALIDATE_INPUT(
              "init() with size is only valid for list, text, or data fields.", type.which());
          break;
      }
      break;
    }
  }

  // Failed.
  return getImpl(builder, member);
}

DynamicValue::Builder DynamicStruct::Builder::initImpl(
    internal::StructBuilder builder, StructSchema::Member member) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      FAIL_VALIDATE_INPUT(
          "Can't init() a union.  get() it first and then init() one of its members.");
      break;

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto type = member.getProto().getBody().getFieldMember().getType().getBody();
      VALIDATE_INPUT(type.which() == schema::Type::Body::STRUCT_TYPE,
                     "init() without a size is only valid for struct fields.") {
        break;
      }
      return initFieldImpl(builder, member,
          member.getContainingStruct().getDependency(type.getStructType()).asStruct());
    }
  }

  // Failed.
  return getImpl(builder, member);
}
DynamicStruct::Builder DynamicStruct::Builder::initFieldImpl(
    internal::StructBuilder builder, StructSchema::Member field, StructSchema type) {
  return DynamicStruct::Builder(
      type, builder.initStructField(
          field.getProto().getBody().getFieldMember().getOffset() * REFERENCES,
          structSizeFromSchema(type)));
}
DynamicList::Builder DynamicStruct::Builder::initFieldImpl(
    internal::StructBuilder builder, StructSchema::Member field,
    ListSchema type, uint size) {
  if (type.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
    return DynamicList::Builder(
        type, builder.initStructListField(
            field.getProto().getBody().getFieldMember().getOffset() * REFERENCES, size * ELEMENTS,
            structSizeFromSchema(type.getStructElementType())));
  } else {
    return DynamicList::Builder(
        type, builder.initListField(
            field.getProto().getBody().getFieldMember().getOffset() * REFERENCES,
            elementSizeFor(type.whichElementType()),
            size * ELEMENTS));
  }
}
Text::Builder DynamicStruct::Builder::initFieldAsTextImpl(
    internal::StructBuilder builder, StructSchema::Member field, uint size) {
  return builder.initBlobField<Text>(
      field.getProto().getBody().getFieldMember().getOffset() * REFERENCES, size * BYTES);
}
Data::Builder DynamicStruct::Builder::initFieldAsDataImpl(
    internal::StructBuilder builder, StructSchema::Member field, uint size) {
  return builder.initBlobField<Data>(
      field.getProto().getBody().getFieldMember().getOffset() * REFERENCES, size * BYTES);
}

// =======================================================================================

DynamicValue::Reader DynamicList::Reader::operator[](uint index) {
  PRECOND(index < size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
#define HANDLE_TYPE(name, discrim, typeName) \
    case schema::Type::Body::discrim##_TYPE: \
      return DynamicValue::Reader(reader.getDataElement<typeName>(index * ELEMENTS));

    HANDLE_TYPE(void, VOID, Void)
    HANDLE_TYPE(bool, BOOL, bool)
    HANDLE_TYPE(int8, INT8, int8_t)
    HANDLE_TYPE(int16, INT16, int16_t)
    HANDLE_TYPE(int32, INT32, int32_t)
    HANDLE_TYPE(int64, INT64, int64_t)
    HANDLE_TYPE(uint8, UINT8, uint8_t)
    HANDLE_TYPE(uint16, UINT16, uint16_t)
    HANDLE_TYPE(uint32, UINT32, uint32_t)
    HANDLE_TYPE(uint64, UINT64, uint64_t)
    HANDLE_TYPE(float32, FLOAT32, float)
    HANDLE_TYPE(float64, FLOAT64, double)
#undef HANDLE_TYPE

    case schema::Type::Body::TEXT_TYPE:
      return DynamicValue::Reader(reader.getBlobElement<Text>(index * ELEMENTS));
    case schema::Type::Body::DATA_TYPE:
      return DynamicValue::Reader(reader.getBlobElement<Data>(index * ELEMENTS));

    case schema::Type::Body::LIST_TYPE: {
      auto elementType = schema.getListElementType();
      return DynamicValue::Reader(DynamicList::Reader(
          elementType, reader.getListElement(
              index * ELEMENTS, elementSizeFor(elementType.whichElementType()))));
    }

    case schema::Type::Body::STRUCT_TYPE:
      return DynamicValue::Reader(DynamicStruct::Reader(
          schema.getStructElementType(), reader.getStructElement(index * ELEMENTS)));

    case schema::Type::Body::ENUM_TYPE:
      return DynamicValue::Reader(DynamicEnum(
          schema.getEnumElementType(), reader.getDataElement<uint16_t>(index * ELEMENTS)));

    case schema::Type::Body::OBJECT_TYPE:
      return DynamicValue::Reader(DynamicObject(
          reader.getObjectElement(index * ELEMENTS)));

    case schema::Type::Body::INTERFACE_TYPE:
      FAIL_RECOVERABLE_CHECK("Interfaces not implemented.") {}
      return DynamicValue::Reader();
  }

  FAIL_CHECK("switch() missing case.", schema.whichElementType());
  return DynamicValue::Reader();
}

DynamicValue::Builder DynamicList::Builder::operator[](uint index) {
  PRECOND(index < size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
#define HANDLE_TYPE(name, discrim, typeName) \
    case schema::Type::Body::discrim##_TYPE: \
      return DynamicValue::Builder(builder.getDataElement<typeName>(index * ELEMENTS));

    HANDLE_TYPE(void, VOID, Void)
    HANDLE_TYPE(bool, BOOL, bool)
    HANDLE_TYPE(int8, INT8, int8_t)
    HANDLE_TYPE(int16, INT16, int16_t)
    HANDLE_TYPE(int32, INT32, int32_t)
    HANDLE_TYPE(int64, INT64, int64_t)
    HANDLE_TYPE(uint8, UINT8, uint8_t)
    HANDLE_TYPE(uint16, UINT16, uint16_t)
    HANDLE_TYPE(uint32, UINT32, uint32_t)
    HANDLE_TYPE(uint64, UINT64, uint64_t)
    HANDLE_TYPE(float32, FLOAT32, float)
    HANDLE_TYPE(float64, FLOAT64, double)
#undef HANDLE_TYPE

    case schema::Type::Body::TEXT_TYPE:
      return DynamicValue::Builder(builder.getBlobElement<Text>(index * ELEMENTS));
    case schema::Type::Body::DATA_TYPE:
      return DynamicValue::Builder(builder.getBlobElement<Data>(index * ELEMENTS));

    case schema::Type::Body::LIST_TYPE:
      return DynamicValue::Builder(DynamicList::Builder(
          schema.getListElementType(), builder.getListElement(index * ELEMENTS)));

    case schema::Type::Body::STRUCT_TYPE:
      return DynamicValue::Builder(DynamicStruct::Builder(
          schema.getStructElementType(), builder.getStructElement(index * ELEMENTS)));

    case schema::Type::Body::ENUM_TYPE:
      return DynamicValue::Builder(DynamicEnum(
          schema.getEnumElementType(), builder.getDataElement<uint16_t>(index * ELEMENTS)));

    case schema::Type::Body::OBJECT_TYPE:
      FAIL_CHECK("List(Object) not supported.");
      break;

    case schema::Type::Body::INTERFACE_TYPE:
      FAIL_RECOVERABLE_CHECK("Interfaces not implemented.") {}
      return DynamicValue::Builder();
  }

  FAIL_CHECK("switch() missing case.", schema.whichElementType());
  return DynamicValue::Builder();
}

void DynamicList::Builder::set(uint index, DynamicValue::Reader value) {
  PRECOND(index < size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
#define HANDLE_TYPE(name, discrim, typeName) \
    case schema::Type::Body::discrim##_TYPE: \
      builder.setDataElement<typeName>(index * ELEMENTS, value.as<typeName>()); \
      break;

    HANDLE_TYPE(void, VOID, Void)
    HANDLE_TYPE(bool, BOOL, bool)
    HANDLE_TYPE(int8, INT8, int8_t)
    HANDLE_TYPE(int16, INT16, int16_t)
    HANDLE_TYPE(int32, INT32, int32_t)
    HANDLE_TYPE(int64, INT64, int64_t)
    HANDLE_TYPE(uint8, UINT8, uint8_t)
    HANDLE_TYPE(uint16, UINT16, uint16_t)
    HANDLE_TYPE(uint32, UINT32, uint32_t)
    HANDLE_TYPE(uint64, UINT64, uint64_t)
    HANDLE_TYPE(float32, FLOAT32, float)
    HANDLE_TYPE(float64, FLOAT64, double)
#undef HANDLE_TYPE

    case schema::Type::Body::TEXT_TYPE:
      builder.setBlobElement<Text>(index * ELEMENTS, value.as<Text>());
      break;
    case schema::Type::Body::DATA_TYPE:
      builder.setBlobElement<Data>(index * ELEMENTS, value.as<Data>());
      break;

    case schema::Type::Body::LIST_TYPE: {
      // TODO(now):  Perform schemaless copy.
      auto listValue = value.as<DynamicList>();
      init(index, listValue.size()).as<DynamicList>().copyFrom(listValue);
      break;
    }

    case schema::Type::Body::STRUCT_TYPE:
      // Note we can't do a schemaless copy here because the space is already allocated.
      DynamicStruct::Builder(
          schema.getStructElementType(), builder.getStructElement(index * ELEMENTS))
          .copyFrom(value.as<DynamicStruct>());
      break;

    case schema::Type::Body::ENUM_TYPE: {
      auto enumValue = value.as<DynamicEnum>();
      VALIDATE_INPUT(schema.getEnumElementType() == enumValue.getSchema(),
                     "Type mismatch when using DynamicList::Builder::set().");
      builder.setDataElement<uint16_t>(index * ELEMENTS, value.as<DynamicEnum>().getRaw());
      break;
    }

    case schema::Type::Body::OBJECT_TYPE:
      FAIL_CHECK("List(Object) not supported.");
      break;

    case schema::Type::Body::INTERFACE_TYPE:
      FAIL_RECOVERABLE_CHECK("Interfaces not implemented.") {}
      break;
  }
}

DynamicValue::Builder DynamicList::Builder::init(uint index, uint size) {
  PRECOND(index < this->size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
    case schema::Type::Body::VOID_TYPE:
    case schema::Type::Body::BOOL_TYPE:
    case schema::Type::Body::INT8_TYPE:
    case schema::Type::Body::INT16_TYPE:
    case schema::Type::Body::INT32_TYPE:
    case schema::Type::Body::INT64_TYPE:
    case schema::Type::Body::UINT8_TYPE:
    case schema::Type::Body::UINT16_TYPE:
    case schema::Type::Body::UINT32_TYPE:
    case schema::Type::Body::UINT64_TYPE:
    case schema::Type::Body::FLOAT32_TYPE:
    case schema::Type::Body::FLOAT64_TYPE:
    case schema::Type::Body::ENUM_TYPE:
    case schema::Type::Body::STRUCT_TYPE:
    case schema::Type::Body::INTERFACE_TYPE:
      FAIL_VALIDATE_INPUT("Expected a list or blob.");
      return DynamicValue::Builder();

    case schema::Type::Body::TEXT_TYPE:
      return DynamicValue::Builder(builder.initBlobElement<Text>(index * ELEMENTS, size * BYTES));

    case schema::Type::Body::DATA_TYPE:
      return DynamicValue::Builder(builder.initBlobElement<Data>(index * ELEMENTS, size * BYTES));

    case schema::Type::Body::LIST_TYPE: {
      auto elementType = schema.getListElementType();

      if (elementType.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
        return DynamicValue::Builder(DynamicList::Builder(
            elementType, builder.initStructListElement(
                index * ELEMENTS, size * ELEMENTS,
                structSizeFromSchema(elementType.getStructElementType()))));
      } else {
        return DynamicValue::Builder(DynamicList::Builder(
            elementType, builder.initListElement(
                index * ELEMENTS, elementSizeFor(elementType.whichElementType()),
                size * ELEMENTS)));
      }
    }

    case schema::Type::Body::OBJECT_TYPE: {
      FAIL_CHECK("List(Object) not supported.");
      return DynamicValue::Builder();
    }
  }

  FAIL_CHECK("switch() missing case.", schema.whichElementType());
  return DynamicValue::Builder();
}

void DynamicList::Builder::copyFrom(Reader other) {
  // TODO(now): copyFrom on ListBuilder.
  // TODO(now): don't forget to check types match.
  FAIL_CHECK("Unimplemented: copyFrom()");
}

void DynamicList::Builder::copyFrom(std::initializer_list<DynamicValue::Reader> value) {
  PRECOND(value.size() == size(), "DynamicList::copyFrom() argument had different size.");
  uint i = 0;
  for (auto element: value) {
    set(i++, element);
  }
}

DynamicList::Reader DynamicList::Builder::asReader() {
  return DynamicList::Reader(schema, builder.asReader());
}

void DynamicList::Reader::verifySchema(ListSchema expectedSchema) {
  VALIDATE_INPUT(schema == expectedSchema,
                 "Type mismatch when using DynamicList::Reader::as().");
}
void DynamicList::Builder::verifySchema(ListSchema expectedSchema) {
  VALIDATE_INPUT(schema == expectedSchema,
                 "Type mismatch when using DynamicList::Reader::as().");
}

// =======================================================================================

namespace {

template <typename T>
T signedToUnsigned(long long value) {
  VALIDATE_INPUT(value >= 0 && T(value) == value,
                 "Value out-of-range for requested type.", value) {
    // Use it anyway.
  }
  return value;
}

template <>
uint64_t signedToUnsigned<uint64_t>(long long value) {
  VALIDATE_INPUT(value >= 0, "Value out-of-range for requested type.", value) {
    // Use it anyway.
  }
  return value;
}

template <typename T>
T unsignedToSigned(unsigned long long value) {
  VALIDATE_INPUT(T(value) >= 0 && (unsigned long long)T(value) == value,
                 "Value out-of-range for requested type.", value) {
    // Use it anyway.
  }
  return value;
}

template <>
int64_t unsignedToSigned<int64_t>(unsigned long long value) {
  VALIDATE_INPUT(int64_t(value) >= 0, "Value out-of-range for requested type.", value) {
    // Use it anyway.
  }
  return value;
}

template <typename T, typename U>
T checkRoundTrip(U value) {
  VALIDATE_INPUT(T(value) == value,
                 "Value out-of-range for requested type.", value) {
    // Use it anyway.
  }
  return value;
}

}  // namespace

#define HANDLE_NUMERIC_TYPE(typeName, ifInt, ifUint, ifFloat) \
typeName DynamicValue::Reader::AsImpl<typeName>::apply(Reader reader) { \
  switch (reader.type) { \
    case INT: \
      return ifInt<typeName>(reader.intValue); \
    case UINT: \
      return ifUint<typeName>(reader.uintValue); \
    case FLOAT: \
      return ifFloat<typeName>(reader.floatValue); \
    default: \
      FAIL_VALIDATE_INPUT("Type mismatch when using DynamicValue::Reader::as()."); \
      return 0; \
  } \
} \
typeName DynamicValue::Builder::AsImpl<typeName>::apply(Builder builder) { \
  switch (builder.type) { \
    case INT: \
      return ifInt<typeName>(builder.intValue); \
    case UINT: \
      return ifUint<typeName>(builder.uintValue); \
    case FLOAT: \
      return ifFloat<typeName>(builder.floatValue); \
    default: \
      FAIL_VALIDATE_INPUT("Type mismatch when using DynamicValue::Builder::as()."); \
      return 0; \
  } \
}

HANDLE_NUMERIC_TYPE(int8_t, checkRoundTrip, unsignedToSigned, checkRoundTrip)
HANDLE_NUMERIC_TYPE(int16_t, checkRoundTrip, unsignedToSigned, checkRoundTrip)
HANDLE_NUMERIC_TYPE(int32_t, checkRoundTrip, unsignedToSigned, checkRoundTrip)
HANDLE_NUMERIC_TYPE(int64_t, implicit_cast, unsignedToSigned, checkRoundTrip)
HANDLE_NUMERIC_TYPE(uint8_t, signedToUnsigned, checkRoundTrip, checkRoundTrip)
HANDLE_NUMERIC_TYPE(uint16_t, signedToUnsigned, checkRoundTrip, checkRoundTrip)
HANDLE_NUMERIC_TYPE(uint32_t, signedToUnsigned, checkRoundTrip, checkRoundTrip)
HANDLE_NUMERIC_TYPE(uint64_t, signedToUnsigned, implicit_cast, checkRoundTrip)
HANDLE_NUMERIC_TYPE(float, implicit_cast, implicit_cast, implicit_cast)
HANDLE_NUMERIC_TYPE(double, implicit_cast, implicit_cast, implicit_cast)

#undef HANDLE_NUMERIC_TYPE

#define HANDLE_TYPE(name, discrim, typeName) \
ReaderFor<typeName> DynamicValue::Reader::AsImpl<typeName>::apply(Reader reader) { \
  VALIDATE_INPUT(reader.type == discrim, \
      "Type mismatch when using DynamicValue::Reader::as().") { \
    return ReaderFor<typeName>(); \
  } \
  return reader.name##Value; \
} \
BuilderFor<typeName> DynamicValue::Builder::AsImpl<typeName>::apply(Builder builder) { \
  VALIDATE_INPUT(builder.type == discrim, \
      "Type mismatch when using DynamicValue::Builder::as().") { \
    return BuilderFor<typeName>(); \
  } \
  return builder.name##Value; \
}

//HANDLE_TYPE(void, VOID, Void)
HANDLE_TYPE(bool, BOOL, bool)

HANDLE_TYPE(text, TEXT, Text)
HANDLE_TYPE(list, LIST, DynamicList)
HANDLE_TYPE(struct, STRUCT, DynamicStruct)
HANDLE_TYPE(enum, ENUM, DynamicEnum)
HANDLE_TYPE(object, OBJECT, DynamicObject)
HANDLE_TYPE(union, UNION, DynamicUnion)

#undef HANDLE_TYPE

Data::Reader DynamicValue::Reader::AsImpl<Data>::apply(Reader reader) {
  if (reader.type == TEXT) {
    // Implicitly convert from text.
    return reader.textValue;
  }
  VALIDATE_INPUT(reader.type == DATA,
      "Type mismatch when using DynamicValue::Reader::as().") {
    return Data::Reader();
  }
  return reader.dataValue;
}
Data::Builder DynamicValue::Builder::AsImpl<Data>::apply(Builder builder) {
  if (builder.type == TEXT) {
    // Implicitly convert from text.
    return builder.textValue;
  }
  VALIDATE_INPUT(builder.type == DATA,
      "Type mismatch when using DynamicValue::Builder::as().") {
    return Data::Builder();
  }
  return builder.dataValue;
}


// As in the header, HANDLE_TYPE(void, VOID, Void) crashes GCC 4.7.
Void DynamicValue::Reader::AsImpl<Void>::apply(Reader reader) {
  VALIDATE_INPUT(reader.type == VOID,
      "Type mismatch when using DynamicValue::Reader::as().") {
    return Void();
  }
  return reader.voidValue;
}
Void DynamicValue::Builder::AsImpl<Void>::apply(Builder builder) {
  VALIDATE_INPUT(builder.type == VOID,
      "Type mismatch when using DynamicValue::Builder::as().") {
    return Void();
  }
  return builder.voidValue;
}

// =======================================================================================

template <>
DynamicStruct::Reader MessageReader::getRoot<DynamicStruct>(StructSchema schema) {
  return DynamicStruct::Reader(schema, getRootInternal());
}

template <>
DynamicStruct::Builder MessageBuilder::initRoot<DynamicStruct>(StructSchema schema) {
  return DynamicStruct::Builder(schema, initRoot(structSizeFromSchema(schema)));
}

template <>
DynamicStruct::Builder MessageBuilder::getRoot<DynamicStruct>(StructSchema schema) {
  return DynamicStruct::Builder(schema, getRoot(structSizeFromSchema(schema)));
}

namespace internal {

DynamicStruct::Reader PointerHelpers<DynamicStruct, Kind::UNKNOWN>::getDynamic(
    StructReader reader, WireReferenceCount index, StructSchema schema) {
  return DynamicStruct::Reader(schema, reader.getStructField(index, nullptr));
}
DynamicStruct::Builder PointerHelpers<DynamicStruct, Kind::UNKNOWN>::getDynamic(
    StructBuilder builder, WireReferenceCount index, StructSchema schema) {
  return DynamicStruct::Builder(schema, builder.getStructField(
      index, structSizeFromSchema(schema), nullptr));
}
void PointerHelpers<DynamicStruct, Kind::UNKNOWN>::set(
    StructBuilder builder, WireReferenceCount index, DynamicStruct::Reader value) {
  // TODO(now):  schemaless copy
  FAIL_CHECK("Unimplemented: copyFrom()");
}
DynamicStruct::Builder PointerHelpers<DynamicStruct, Kind::UNKNOWN>::init(
    StructBuilder builder, WireReferenceCount index, StructSchema schema) {
  return DynamicStruct::Builder(schema,
      builder.initStructField(index, structSizeFromSchema(schema)));
}

DynamicList::Reader PointerHelpers<DynamicList, Kind::UNKNOWN>::getDynamic(
    StructReader reader, WireReferenceCount index, ListSchema schema) {
  return DynamicList::Reader(schema,
      reader.getListField(index, elementSizeFor(schema.whichElementType()), nullptr));
}
DynamicList::Builder PointerHelpers<DynamicList, Kind::UNKNOWN>::getDynamic(
    StructBuilder builder, WireReferenceCount index, ListSchema schema) {
  return DynamicList::Builder(schema, builder.getListField(index, nullptr));
}
void PointerHelpers<DynamicList, Kind::UNKNOWN>::set(
    StructBuilder builder, WireReferenceCount index, DynamicList::Reader value) {
  // TODO(now):  schemaless copy
  FAIL_CHECK("Unimplemented: copyFrom()");
}
DynamicList::Builder PointerHelpers<DynamicList, Kind::UNKNOWN>::init(
    StructBuilder builder, WireReferenceCount index, ListSchema schema, uint size) {
  if (schema.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
    return DynamicList::Builder(schema,
        builder.initStructListField(index, size * ELEMENTS,
            structSizeFromSchema(schema.getStructElementType())));
  } else {
    return DynamicList::Builder(schema,
        builder.initListField(index, elementSizeFor(schema.whichElementType()), size * ELEMENTS));
  }
}

}  // namespace internal

}  // namespace capnproto