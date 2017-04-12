/******************************************************************************

  This source file is part of the Avogadro project.

  Copyright 2013 Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/

#include "xyzformat.h"

#include <avogadro/core/elements.h>
#include <avogadro/core/molecule.h>
#include <avogadro/core/utilities.h>
#include <avogadro/core/vector.h>

#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

using std::string;
using std::endl;
using std::getline;
using std::string;
using std::vector;

namespace Avogadro {
namespace Io {

using Core::Array;
using Core::Atom;
using Core::Elements;
using Core::Molecule;
using Core::lexicalCast;
using Core::split;
using Core::trimmed;

#ifndef _WIN32
using std::isalpha;
#endif

XyzFormat::XyzFormat()
{
}

XyzFormat::~XyzFormat()
{
}

bool XyzFormat::read(std::istream& inStream, Core::Molecule& mol)
{
  size_t numAtoms = 0;
  if (!(inStream >> numAtoms)) {
    appendError("Error parsing number of atoms.");
    return false;
  }

  string buffer;
  getline(inStream, buffer); // Finish the first line
  getline(inStream, buffer);
  if (!buffer.empty())
    mol.setData("name", trimmed(buffer));

  // Parse atoms
  for (size_t i = 0; i < numAtoms; ++i) {
    getline(inStream, buffer);
    vector<string> tokens(split(buffer, ' '));

    if (tokens.size() < 4) {
      appendError("Not enough tokens in this line: " + buffer);
      return false;
    }

    unsigned char atomicNum(0);
    if (isalpha(tokens[0][0]))
      atomicNum = Elements::atomicNumberFromSymbol(tokens[0]);
    else
      atomicNum = static_cast<unsigned char>(lexicalCast<short int>(tokens[0]));

    Vector3 pos(lexicalCast<double>(tokens[1]), lexicalCast<double>(tokens[2]),
                lexicalCast<double>(tokens[3]));

    Atom newAtom = mol.addAtom(atomicNum);
    newAtom.setPosition3d(pos);
  }

  // Check that all atoms were handled.
  if (mol.atomCount() != numAtoms) {
    std::ostringstream errorStream;
    errorStream << "Error parsing atom at index " << mol.atomCount()
                << " (line " << 3 + mol.atomCount() << ").\n"
                << buffer;
    appendError(errorStream.str());
    return false;
  }

  // Do we have an animation?
  size_t numAtoms2;
  if (getline(inStream, buffer) && (numAtoms2 = lexicalCast<int>(buffer)) &&
      numAtoms == numAtoms2) {
    getline(inStream, buffer); // Skip the blank
    mol.setCoordinate3d(mol.atomPositions3d(), 0);
    int coordSet = 1;
    while (numAtoms == numAtoms2) {
      Array<Vector3> positions;
      positions.reserve(numAtoms);

      for (size_t i = 0; i < numAtoms; ++i) {
        getline(inStream, buffer);
        vector<string> tokens(split(buffer, ' '));
        if (tokens.size() < 4) {
          appendError("Not enough tokens in this line: " + buffer);
          return false;
        }
        Vector3 pos(lexicalCast<double>(tokens[1]),
                    lexicalCast<double>(tokens[2]),
                    lexicalCast<double>(tokens[3]));
        positions.push_back(pos);
      }

      mol.setCoordinate3d(positions, coordSet++);

      if (!getline(inStream, buffer)) {
        numAtoms2 = lexicalCast<int>(buffer);
        if (numAtoms == numAtoms2)
          break;
      }

      std::getline(inStream, buffer); // Skip the blank
      positions.clear();
    }
  }

  // This format has no connectivity information, so perceive basics at least.
  mol.perceiveBondsSimple();

  return true;
}

bool XyzFormat::write(std::ostream& outStream, const Core::Molecule& mol)
{
  size_t numAtoms = mol.atomCount();

  outStream << numAtoms << std::endl;
  if (mol.data("name").toString().length())
    outStream << mol.data("name").toString() << endl;
  else
    outStream << "XYZ file generated by Avogadro.\n";

  for (size_t i = 0; i < numAtoms; ++i) {
    Atom atom = mol.atom(i);
    if (!atom.isValid()) {
      appendError("Internal error: Atom invalid.");
      return false;
    }

    outStream << std::setw(3) << std::left
              << Elements::symbol(atom.atomicNumber()) << " " << std::setw(10)
              << std::right << std::fixed << std::setprecision(5)
              << atom.position3d().x() << " " << std::setw(10) << std::right
              << std::fixed << std::setprecision(5) << atom.position3d().y()
              << " " << std::setw(10) << std::right << std::fixed
              << std::setprecision(5) << atom.position3d().z() << "\n";
  }

  return true;
}

std::vector<std::string> XyzFormat::fileExtensions() const
{
  std::vector<std::string> ext;
  ext.push_back("xyz");
  return ext;
}

std::vector<std::string> XyzFormat::mimeTypes() const
{
  std::vector<std::string> mime;
  mime.push_back("chemical/x-xyz");
  return mime;
}

} // end Io namespace
} // end Avogadro namespace
